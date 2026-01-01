#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_5.c
 *
 * Пакет #5: "геометрия + градиенты" (5 эффектов)
 *  - без float
 *  - без больших буферов
 *  - render() только рисует в буфер
 * ============================================================ */

/* -------------------- helpers -------------------- */

static inline uint32_t speed_mul(const fx_ctx_t *ctx)
{
    uint32_t sp = (uint32_t)ctx->speed_pct;
    if (sp < 10) sp = 10;
    if (sp > 300) sp = 300;
    return sp;
}

/* xorshift32 RNG */
static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* tri wave 0..255..0 */
static inline uint8_t tri_u8(uint16_t p)
{
    p &= 0x01FFu;
    if (p & 0x0100u) p = 0x01FFu - p;
    return (uint8_t)p;
}

/* abs for small ints */
static inline uint16_t u16_abs_i16(int16_t v)
{
    return (uint16_t)(v < 0 ? -v : v);
}

/* Fast HSV->RGB (0..255) */
static inline void hsv_to_rgb_u8(uint8_t h, uint8_t s, uint8_t v,
                                uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = v; *g = v; *b = v; return; }

    const uint8_t region = h / 43;
    const uint8_t rem    = (h - region * 43) * 6;

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

/* ------------------------------------------------------------
 * FX #1: DIAGONAL RAINBOW (движущийся диагональный градиент)
 * ------------------------------------------------------------ */
void fx_diag_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + (x * 7u) + (y * 11u));
            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, 255, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #2: RADIAL PULSE (пульс из центра, кольца)
 *   Без sqrt: используем L1 distance (ромб), выглядит "хардкорно" и красиво.
 * ------------------------------------------------------------ */
void fx_radial_pulse_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (4u * 100u / sp + 1u);

    const int16_t cx = (int16_t)(MATRIX_W / 2u);
    const int16_t cy = (int16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const int16_t dx = (int16_t)x - cx;
            const int16_t dy = (int16_t)y - cy;

            /* “дистанция” ромбом */
            const uint16_t d = (uint16_t)(u16_abs_i16(dx) + u16_abs_i16(dy));

            /* кольца: три-волна по (phase - d*step) */
            const uint16_t p = (uint16_t)((phase - (uint32_t)d * 9u) & 0x01FFu);
            const uint8_t  v = tri_u8(p);

            uint8_t r, g, b;
            hsv_to_rgb_u8((uint8_t)(phase >> 2), 200, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #3: CHECKER FLOW (шахматка, цвет перетекает)
 * ------------------------------------------------------------ */
void fx_checker_flow_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (5u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t cell = (uint8_t)((x ^ y) & 1u);

            /* две палитры */
            const uint8_t h = (uint8_t)(phase + (cell ? 90u : 0u));
            const uint8_t v = (uint8_t)(cell ? 255u : 140u);

            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #4: METEORS (несколько “метеоров” по диагонали)
 *   Без хранения массивов: рисуем N метеоров, каждый кадр заново.
 * ------------------------------------------------------------ */
void fx_meteors_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (2u * 100u / sp + 1u);

    /* 4 метеора достаточно для 48x16 */
    const uint8_t meteors = 4;
    const uint8_t tail = 10;

    for (uint8_t i = 0; i < meteors; i++) {
        /* стартовые сдвиги */
        const uint32_t p = phase + (uint32_t)i * 97u;

        /* позиция по диагонали в “вирт. линии” длиной W+H */
        const uint16_t line = (uint16_t)(p % (uint32_t)(MATRIX_W + MATRIX_H));

        /* диагональ: x растёт, y растёт, с заворотом */
        int16_t x0 = (int16_t)line;
        int16_t y0 = 0;
        if (x0 >= (int16_t)MATRIX_W) {
            y0 = (int16_t)(x0 - (int16_t)MATRIX_W + 1);
            x0 = (int16_t)MATRIX_W - 1;
        }

        /* цвет по i */
        uint8_t r0, g0, b0;
        hsv_to_rgb_u8((uint8_t)(p >> 1), 220, 255, &r0, &g0, &b0);

        /* хвост */
        for (uint8_t k = 0; k <= tail; k++) {
            const int16_t xx = x0 - (int16_t)k;
            const int16_t yy = y0 + (int16_t)k;

            if (xx < 0 || yy < 0) continue;
            if ((uint16_t)xx >= MATRIX_W || (uint16_t)yy >= MATRIX_H) continue;

            const uint8_t fade = (uint8_t)(255u - (uint16_t)k * (255u / (tail + 1u)));

            matrix_ws2812_set_pixel_xy((uint16_t)xx, (uint16_t)yy,
                                       (uint8_t)((uint16_t)r0 * fade / 255u),
                                       (uint8_t)((uint16_t)g0 * fade / 255u),
                                       (uint8_t)((uint16_t)b0 * fade / 255u));
        }
    }
}

/* ------------------------------------------------------------
 * FX #5: GLITTER RAINBOW (радуга + редкий белый глиттер)
 * ------------------------------------------------------------ */
void fx_glitter_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    /* базовая радуга по X */
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + x * 6u);
            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, 255, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }

    /* глиттер: случайные белые точки поверх (без состояния) */
    uint32_t s = (uint32_t)(0x9E3779B9u ^ (phase * 33u));
    const uint16_t sparks = (uint16_t)((MATRIX_LEDS_TOTAL / 64u) + 2u);

    for (uint16_t i = 0; i < sparks; i++) {
        uint32_t r0 = xs32(&s);
        const uint16_t x = (uint16_t)(r0 % MATRIX_W);
        const uint16_t y = (uint16_t)((r0 / MATRIX_W) % MATRIX_H);

        /* редкость: пропускаем часть */
        if ((r0 & 0x3u) != 0) continue;

        matrix_ws2812_set_pixel_xy(x, y, 255, 255, 255);
    }
}
