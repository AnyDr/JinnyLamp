#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_classic_4.c
 *
 * Пакет #4: 5 простых эффектов без float и без тяжёлых зависимостей.
 *
 * Инварианты проекта:
 *  - render() только пишет в буфер (set_pixel_xy/clear)
 *  - show() вызывается снаружи (matrix_task)
 *  - яркость уже масштабируется внутри matrix_ws2812_set_pixel_xy()
 * ============================================================ */

/* -------------------- small helpers -------------------- */

/* Очень лёгкий RNG (детерминированный, без malloc/printf). */
static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* "Треугольная" волна 0..255..0 по фазе 0..511 */
static inline uint8_t tri_u8(uint16_t p)
{
    p &= 0x01FFu;
    if (p & 0x0100u) p = 0x01FFu - p;
    return (uint8_t)p;
}

/* Быстрый HSV->RGB (0..255) без float.
 * Нормально подходит для “радужных” эффектов.
 */
static inline void hsv_to_rgb_u8(uint8_t h, uint8_t s, uint8_t v,
                                uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = v; *g = v; *b = v; return; }

    const uint8_t region = h / 43;            /* 0..5 */
    const uint8_t rem    = (h - region * 43) * 6; /* 0..258-ish */

    const uint8_t p = (uint8_t)((uint16_t)v * (255 - s) / 255);
    const uint8_t q = (uint8_t)((uint16_t)v * (255 - ((uint16_t)s * rem) / 255) / 255);
    const uint8_t t = (uint8_t)((uint16_t)v * (255 - ((uint16_t)s * (255 - rem)) / 255) / 255);

    switch (region) {
        default:
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

/* Скорость: базовый шаг эффекта * speed_pct.
 * Примечание: ctx->phase у тебя уже крутится движком, но этот множитель
 * помогает “нормализовать стартовую скорость” по твоей системе.
 */
static inline uint32_t speed_mul(const fx_ctx_t *ctx)
{
    /* speed_pct: 10..300 */
    uint32_t sp = (uint32_t)ctx->speed_pct;
    if (sp < 10) sp = 10;
    if (sp > 300) sp = 300;
    return sp;
}

/* ============================================================
 * FX #1: COLOR WIPE (заливка “ползёт” по виртуальной матрице)
 * ============================================================ */
void fx_color_wipe_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t total = (uint32_t)MATRIX_W * (uint32_t)MATRIX_H;
    if (total == 0) return;

    /* Позиция заливки: 0..total-1 */
    const uint32_t pos = (ctx->phase / (8u * 100u / sp + 1u)) % total;

    /* Цвет: медленно крутим hue */
    uint8_t r, g, b;
    hsv_to_rgb_u8((uint8_t)(ctx->phase >> 8), 255, 255, &r, &g, &b);

    for (uint32_t i = 0; i < total; i++) {
        const uint16_t x = (uint16_t)(i % MATRIX_W);
        const uint16_t y = (uint16_t)(i / MATRIX_W);
        if (i <= pos) matrix_ws2812_set_pixel_xy(x, y, r, g, b);
    }
}

/* ============================================================
 * FX #2: LARSON SCANNER (бегущий “глаз” с хвостом)
 * ============================================================ */
void fx_larson_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);

    /* Двигаем точку по X туда-сюда */
    const uint16_t range = (MATRIX_W > 1) ? (uint16_t)(MATRIX_W - 1) : 0;
    const uint16_t p = (uint16_t)((ctx->phase / (6u * 100u / sp + 1u)) & 0x01FFu);
    uint16_t x = (range == 0) ? 0 : (uint16_t)((uint32_t)tri_u8(p) * range / 255u);

    /* Цвет: фиксированный “красный лазер” */
    const uint8_t r0 = 255, g0 = 32, b0 = 0;

    /* Хвост: 6 пикселей */
    const uint8_t tail = 6;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint8_t k = 0; k <= tail; k++) {
            int16_t xx = (int16_t)x - (int16_t)k;
            if (xx < 0) continue;
            const uint8_t fade = (uint8_t)(255u - (uint16_t)k * (255u / (tail + 1u)));
            matrix_ws2812_set_pixel_xy((uint16_t)xx, y,
                                       (uint8_t)((uint16_t)r0 * fade / 255u),
                                       (uint8_t)((uint16_t)g0 * fade / 255u),
                                       (uint8_t)((uint16_t)b0 * fade / 255u));
        }
    }
}

/* ============================================================
 * FX #3: BPM BARS (цветные полосы “дышат” по синусу/треугольнику)
 * ============================================================ */
void fx_bpm_bars_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);

    /* “бит” 0..255..0 */
    const uint16_t p = (uint16_t)((ctx->phase / (5u * 100u / sp + 1u)) & 0x01FFu);
    const uint8_t beat = tri_u8(p);

    for (uint16_t x = 0; x < MATRIX_W; x++) {
        uint8_t r, g, b;
        hsv_to_rgb_u8((uint8_t)(x * 255u / (MATRIX_W ? MATRIX_W : 1u)), 255, beat, &r, &g, &b);

        for (uint16_t y = 0; y < MATRIX_H; y++) {
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ============================================================
 * FX #4: TWINKLES (мерцание “звёзд”, без хранения состояния на матрицу)
 * ============================================================ */
void fx_twinkles_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);

    /* Кол-во “звёзд” на кадр: зависит от размера матрицы */
    const uint16_t stars = (uint16_t)((MATRIX_LEDS_TOTAL / 24u) + 6u);

    uint32_t s = (uint32_t)(0xA53C9E21u ^ ctx->phase);

    for (uint16_t i = 0; i < stars; i++) {
        uint32_t r0 = xs32(&s);
        uint16_t x = (uint16_t)(r0 % MATRIX_W);
        uint16_t y = (uint16_t)((r0 / MATRIX_W) % MATRIX_H);

        /* Яркость звезды “дышит” от phase + соли, чтобы не требовалась память */
        uint16_t p = (uint16_t)((ctx->phase / (4u * 100u / sp + 1u) + (r0 >> 16)) & 0x01FFu);
        uint8_t v = tri_u8(p);

        uint8_t rr, gg, bb;
        hsv_to_rgb_u8((uint8_t)(r0 >> 24), 180, v, &rr, &gg, &bb);
        matrix_ws2812_set_pixel_xy(x, y, rr, gg, bb);
    }
}

/* ============================================================
 * FX #5: FIRE SIMPLE (огонь без тяжёлой симуляции, но визуально похож)
 * ============================================================ */
void fx_fire_simple_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);

    /* Внизу “топливо”, сверху затухает */
    uint32_t s = (uint32_t)(0xC3B2E187u ^ (ctx->phase * 17u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        /* Нормализация высоты: y=0 низ */
        const uint16_t yy = (uint16_t)(MATRIX_H - 1u - y);

        for (uint16_t x = 0; x < MATRIX_W; x++) {
            /* База шума */
            uint32_t r0 = xs32(&s);
            uint8_t n = (uint8_t)(r0 & 0xFFu);

            /* “Движение” по времени */
            uint8_t t = (uint8_t)((ctx->phase / (3u * 100u / sp + 1u)) & 0xFFu);

            /* Интенсивность падает кверху */
            uint16_t fall = (uint16_t)(yy * 255u / (MATRIX_H ? MATRIX_H : 1u));
            uint16_t v = (uint16_t)n + (uint16_t)(255u - fall) + (uint16_t)t;
            if (v > 255u) v = 255u;

            /* Палитра огня (примерно): v -> RGB */
            uint8_t rr = (uint8_t)v;
            uint8_t gg = (uint8_t)((v > 80u) ? (v - 80u) * 2u : 0u);
            uint8_t bb = (uint8_t)((v > 200u) ? (v - 200u) : 0u);

            matrix_ws2812_set_pixel_xy(x, y, rr, gg, bb);
        }
    }
}
