#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_8.c
 *
 * Пакет #8: эффекты ID 40..44
 *  - без float
 *  - без больших буферов
 *  - детерминированные (без rand())
 *  - render() пишет в WS2812 framebuffer через matrix_ws2812_set_pixel_xy()
 * ============================================================ */

/* -------------------- helpers -------------------- */

static inline uint32_t speed_mul(const fx_ctx_t *ctx)
{
    uint32_t sp = (uint32_t)ctx->speed_pct;
    if (sp < 10)  sp = 10;
    if (sp > 300) sp = 300;
    return sp;
}

/* tri 0..255..0 */
static inline uint8_t tri_u8(uint16_t p)
{
    p &= 0x01FFu;
    if (p & 0x0100u) p = 0x01FFu - p;
    return (uint8_t)p;
}

static inline uint16_t abs16(int16_t v)
{
    return (uint16_t)(v < 0 ? -v : v);
}

/* xorshift32: маленький PRNG для локальных seed */
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

/* простая “гамма-подобная” кривая без таблиц (поднимаем низы) */
static inline uint8_t boost_u8(uint8_t v)
{
    /* v' = (v*v)/255 */
    return (uint8_t)(((uint16_t)v * (uint16_t)v) / 255u);
}

/* ------------------------------------------------------------
 * FX #1 (ID 40): COLOR WAVES (X)
 *   Волны по X: цвет зависит от (x + phase), яркость чуть дышит по y.
 * ------------------------------------------------------------ */
void fx_color_waves_x_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (4u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        const uint8_t yv = (uint8_t)(180u + (tri_u8((uint16_t)(phase + y * 29u)) >> 2)); /* 180..243 */
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + x * 5u);
            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, yv, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #2 (ID 41): STROBE SOFT
 *   Мягкий строб: вспышки, но без “жёсткого белого”, чтобы не убивать глаза.
 *   Реализовано как импульсы tri, затем boost().
 * ------------------------------------------------------------ */
void fx_strobe_soft_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    /* быстрее speed => чаще импульсы */
    const uint32_t phase = ctx->phase / (1u * 100u / sp + 1u);

    const uint8_t p = tri_u8((uint16_t)(phase * 3u));
    const uint8_t v = boost_u8(p); /* приподнимаем пики */

    /* легкий цветовой сдвиг */
    const uint8_t hue = (uint8_t)(phase * 2u);

    uint8_t r0, g0, b0;
    hsv_to_rgb_u8(hue, 255, v, &r0, &g0, &b0);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, r0, g0, b0);
        }
    }
}

/* ------------------------------------------------------------
 * FX #3 (ID 42): WALKING DOTS
 *   “Рой точек”: несколько ярких точек бегут по полю.
 *   Без хранения состояния: позиции из hash(x,y,phase).
 * ------------------------------------------------------------ */
void fx_walking_dots_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    const uint32_t seed = (uint32_t)(0xD07A2EADu ^ (phase * 11u));

    /* “плотность” точек: чем больше, тем больше шансов вспыхнуть */
    const uint8_t thresh = 14; /* ~5-6 точек на кадр в среднем на 48x16 */

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = hash2_u8(x, y, seed);
            if (h < thresh) {
                /* цвет от позиции + времени */
                const uint8_t hue = (uint8_t)(phase + x * 7u + y * 13u);
                uint8_t r, g, b;
                hsv_to_rgb_u8(hue, 255, 255, &r, &g, &b);
                matrix_ws2812_set_pixel_xy(x, y, r, g, b);
            }
        }
    }
}

/* ------------------------------------------------------------
 * FX #4 (ID 43): VORTEX
 *   Псевдо-вихрь: цвет зависит от “угла”, но без atan2().
 *   Используем приближение: сравнение dx/dy и квадранты.
 * ------------------------------------------------------------ */
void fx_vortex_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (4u * 100u / sp + 1u);

    const int16_t cx = (int16_t)(MATRIX_W / 2u);
    const int16_t cy = (int16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            const int16_t dx = (int16_t)x - cx;
            const int16_t dy = (int16_t)y - cy;

            const uint16_t adx = abs16(dx);
            const uint16_t ady = abs16(dy);

            /* грубая оценка “угла” 0..255 */
            uint8_t a = 0;
            if (dx >= 0 && dy < 0) a = 0;      /* Q1 */
            if (dx >= 0 && dy >= 0) a = 64;    /* Q2 */
            if (dx < 0  && dy >= 0) a = 128;   /* Q3 */
            if (dx < 0  && dy < 0)  a = 192;   /* Q4 */

            /* добавляем “внутриквадрант” по отношению ady/(adx+ady) */
            const uint16_t denom = (uint16_t)(adx + ady + 1u);
            const uint8_t frac = (uint8_t)((uint32_t)ady * 63u / denom);
            a = (uint8_t)(a + frac);

            /* радиальная модуляция */
            const uint16_t dist = (uint16_t)(adx + ady);
            const uint8_t v = (uint8_t)(120u + (tri_u8((uint16_t)(phase + dist * 18u)) >> 1));

            uint8_t r, g, b;
            hsv_to_rgb_u8((uint8_t)(a + phase), 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #5 (ID 44): HYPER GRID
 *   “Сетка” (вертик/горизонт линии) с бегущей фазой и сменой цвета.
 * ------------------------------------------------------------ */
void fx_hyper_grid_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    /* шаг сетки */
    const uint8_t step = 4; /* 4 пикселя */

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            const bool grid = ((x % step) == 0u) || ((y % step) == 0u);

            if (!grid) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
                continue;
            }

            const uint8_t h = (uint8_t)(phase + x * 9u + y * 7u);
            const uint8_t v = (uint8_t)(140u + (tri_u8((uint16_t)(phase + (x + y) * 21u)) >> 2));

            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
