#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_11.c
 *
 * Пакет #11: эффекты ID 55..59
 * Цели:
 *   - простые, стабильные, без float
 *   - без больших RAM буферов
 *   - каждый render рисует весь кадр через matrix_ws2812_set_pixel_xy()
 *   - скорость через ctx->speed_pct
 * ============================================================ */

/* -------------------- helpers -------------------- */

static inline uint32_t speed_mul(const fx_ctx_t *ctx)
{
    uint32_t sp = (uint32_t)ctx->speed_pct;
    if (sp < 10)  sp = 10;
    if (sp > 300) sp = 300;
    return sp;
}

/* tri 0..255..0 (период 512) */
static inline uint8_t tri_u8(uint16_t p)
{
    p &= 0x01FFu;
    if (p & 0x0100u) p = 0x01FFu - p;
    return (uint8_t)p;
}

/* xorshift32 */
static inline uint32_t xs32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* 2D hash -> 8-bit */
static inline uint8_t hash2_u8(uint16_t x, uint16_t y, uint32_t seed)
{
    uint32_t v = seed;
    v ^= (uint32_t)x * 0x9E3779B1u;
    v ^= (uint32_t)y * 0x85EBCA77u;
    v = xs32(v);
    return (uint8_t)(v & 0xFFu);
}

static inline uint8_t boost_u8(uint8_t v)
{
    /* лёгкий “гамма-буст” без LUT */
    return (uint8_t)(((uint16_t)v * (uint16_t)v) / 255u);
}

/* HSV->RGB (0..255) без float */
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

static inline uint16_t abs_u16(int16_t v)
{
    return (uint16_t)(v < 0 ? -v : v);
}

#if 0
/* ============================================================
 * ID 55: FIREFLIES
 *   “светлячки”: редкие яркие вспышки на фоне.
 *   Без состояния: вероятность вспышки через hash(x,y,phase).
 * ============================================================ */
void fx_fireflies_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (3u * 100u / sp + 1u);

    const uint32_t seed = 0xF11EF155u ^ (ph >> 2);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            const uint8_t n = hash2_u8(x, y, seed);

            /* Фон: слабое “дыхание” */
            uint8_t bg = (uint8_t)(10u + (tri_u8((uint16_t)(ph + x * 7u + y * 11u)) >> 4));
            bg = (uint8_t)(bg * 2u);

            /* Вспышки: чем выше порог, тем реже */
            uint8_t v = bg;
            if (n > 245u) {
                /* очень редкие яркие “точки” */
                uint8_t pop = boost_u8((uint8_t)((n - 245u) * 28u));
                v = (uint8_t)(bg + pop);
                if (v < bg) v = 255; /* защита от переполнения */
            }

            /* Цвет: желто-зелёные светляки */
            const uint8_t hue = (uint8_t)(60u + (n >> 3) + (ph >> 3));
            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
#endif /* duplicated in fx_effects_geo_7.c */

#if 0
/* ============================================================
 * ID 56: CHECKER PULSE
 *   “шахматка”, которая пульсирует и слегка “ползёт”.
 * ============================================================ */
static void fx_checker_pulse_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (4u * 100u / sp + 1u);

    const uint8_t pulse = tri_u8((uint16_t)(ph * 2u)); /* 0..255..0 */
    const uint8_t val_a = (uint8_t)(30u + (pulse * 200u) / 255u);
    const uint8_t val_b = (uint8_t)(10u + (pulse * 80u)  / 255u);

    const uint8_t hue_a = (uint8_t)(ph + 10u);
    const uint8_t hue_b = (uint8_t)(ph + 170u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* “ползучесть” через фазовый сдвиг */
            const uint16_t xx = (uint16_t)(x + (ph >> 3));
            const uint16_t yy = (uint16_t)(y + (ph >> 4));

            const uint8_t cell = (uint8_t)(((xx >> 1) ^ (yy >> 1)) & 1u);

            uint8_t r,g,b;
            if (cell) {
                hsv_to_rgb_u8(hue_a, 255, val_a, &r, &g, &b);
            } else {
                hsv_to_rgb_u8(hue_b, 255, val_b, &r, &g, &b);
            }
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
#endif

/* ============================================================
 * ID 57: SPIRAL RIDGES
 *   “спиральные гребни” вокруг центра: радиус ромбом + твист.
 * ============================================================ */
void fx_spiral_ridges_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (3u * 100u / sp + 1u);

    const int16_t cx = (int16_t)(MATRIX_W / 2u);
    const int16_t cy = (int16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const int16_t dx = (int16_t)x - cx;
            const int16_t dy = (int16_t)y - cy;

            const uint16_t r = (uint16_t)(abs_u16(dx) + abs_u16(dy));
            const uint16_t twist = (uint16_t)((dx * 23) ^ (dy * 31));

            /* гребень: узкая “полоса” на пике триангулятора */
            const uint8_t w = tri_u8((uint16_t)(ph + r * 24u + twist));

            /* v считаем в uint16_t, потом мягко насыщаем и приводим к uint8_t */
            uint16_t v16 = (w > 220u)
             ? (uint16_t)(60u + (uint16_t)(w - 220u) * 8u)
             : (uint16_t)(w >> 3);

            if (v16 > 255u) v16 = 255u;
            const uint8_t v = (uint8_t)v16;


            const uint8_t hue = (uint8_t)(ph + (twist >> 2) + r * 5u);
            uint8_t r8,g8,b8;
            hsv_to_rgb_u8(hue, 255, v, &r8, &g8, &b8);
            matrix_ws2812_set_pixel_xy(x, y, r8, g8, b8);
        }
    }
}

/* ============================================================
 * ID 58: HEAT SHIMMER
 *   “жаркое марево”: вертикальные потоки + мерцание.
 *   Цвет: от красного к жёлтому.
 * ============================================================ */
void fx_heat_shimmer_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (2u * 100u / sp + 1u);

    const uint32_t seed = 0x4E475858u ^ (ph >> 3); /* hex-only */

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* “подъём” по Y (пламя снизу сильнее) */
            const uint8_t base = (uint8_t)((uint32_t)(MATRIX_H - 1u - y) * 255u / (MATRIX_H ? MATRIX_H : 1u));

            /* шум “марева” */
            const uint8_t n = hash2_u8((uint16_t)(x * 3u), (uint16_t)(y * 7u + (ph >> 2)), seed);
            uint8_t v = (uint8_t)((uint16_t)base + (n >> 2));
            if (v < base) v = 255;

            /* цвет: красный->жёлтый */
            const uint8_t hue = (uint8_t)(5u + (v >> 3)); /* 5..~36 */
            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ============================================================
 * ID 59: DIAGONAL WAVES
 *   диагональные волны с мягкой сменой оттенка.
 * ============================================================ */
void fx_diagonal_waves_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (4u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            const uint16_t p = (uint16_t)(ph + x * 13u + y * 19u);
            uint8_t w = tri_u8(p);
            w = boost_u8(w);

            const uint8_t hue = (uint8_t)(ph + (x * 3u) + (y * 2u));
            const uint8_t val = (uint8_t)(20u + (w * 235u) / 255u);

            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, val, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
