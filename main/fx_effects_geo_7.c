#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_7.c
 *
 * Пакет #7: эффекты ID 35..39
 *  - без float
 *  - без больших буферов
 *  - render() только рисует в matrix_ws2812 буфер
 * ============================================================ */

/* -------------------- helpers -------------------- */

static inline uint32_t speed_mul(const fx_ctx_t *ctx)
{
    uint32_t sp = (uint32_t)ctx->speed_pct;
    if (sp < 10)  sp = 10;
    if (sp > 300) sp = 300;
    return sp;
}

/* tri wave 0..255..0 */
static inline uint8_t tri_u8(uint16_t p)
{
    p &= 0x01FFu;
    if (p & 0x0100u) p = 0x01FFu - p;
    return (uint8_t)p;
}

/* abs для int16 */
static inline uint16_t abs16(int16_t v)
{
    return (uint16_t)(v < 0 ? -v : v);
}

/* быстрый 2D hash -> 8-bit (детерминированный псевдошум) */
static inline uint8_t hash2_u8(uint16_t x, uint16_t y, uint32_t seed)
{
    uint32_t v = seed;
    v ^= (uint32_t)x * 0x9E3779B1u;
    v ^= (uint32_t)y * 0x85EBCA77u;
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
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

/* ------------------------------------------------------------
 * FX #1 (ID 35): CHECKER PULSE
 *   Шахматка, которая “дышит” яркостью, и слегка меняет тон.
 * ------------------------------------------------------------ */
void fx_checker_pulse_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (5u * 100u / sp + 1u);

    const uint8_t pulse = tri_u8((uint16_t)phase);              /* 0..255..0 */
    const uint8_t hueA  = (uint8_t)(phase * 1u);
    const uint8_t hueB  = (uint8_t)(phase * 1u + 110u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const bool odd = (((x ^ y) & 1u) != 0u);

            uint8_t r, g, b;
            const uint8_t v = odd ? (uint8_t)((uint16_t)pulse * 3u / 4u) : pulse;

            hsv_to_rgb_u8(odd ? hueB : hueA, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #2 (ID 36): DIAGONAL RAINBOW WIPE
 *   Диагональные “полосы”, бегущие по матрице.
 * ------------------------------------------------------------ */
void fx_diag_rainbow_wipe_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (4u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t d = (uint16_t)(x + y); /* диагональный индекс */
            const uint8_t h  = (uint8_t)(phase + d * 6u);
            const uint8_t v  = (uint8_t)(180u + (tri_u8((uint16_t)(phase + d * 8u)) >> 2)); /* 180..243 */

            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #3 (ID 37): RADIAL RIPPLE
 *   “Кольца” от центра. Метрика Manhattan (быстро, без sqrt).
 * ------------------------------------------------------------ */
void fx_radial_ripple_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    const int16_t cx = (int16_t)(MATRIX_W / 2u);
    const int16_t cy = (int16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t dx = abs16((int16_t)x - cx);
            const uint16_t dy = abs16((int16_t)y - cy);
            const uint16_t dist = (uint16_t)(dx + dy);

            /* кольца: dist*scale + phase */
            const uint8_t v = tri_u8((uint16_t)(phase + dist * 22u));

            /* оттенок тоже гуляет по dist */
            uint8_t r, g, b;
            hsv_to_rgb_u8((uint8_t)(phase + dist * 4u), 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #4 (ID 38): FIREFLIES
 *   Несколько “светлячков” летают по полю, оставляя мягкий след.
 *   Без хранения состояния: позиции генерим из seed + phase.
 * ------------------------------------------------------------ */
void fx_fireflies_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (2u * 100u / sp + 1u);

    const uint8_t count = 10; /* число светлячков */
    uint32_t seed = (uint32_t)(0xBADC0FFEu ^ (phase * 17u));

    for (uint8_t i = 0; i < count; i++) {
        /* простой LCG для детерминированных координат */
        seed = seed * 1664525u + 1013904223u;
        const uint16_t x0 = (uint16_t)(seed % MATRIX_W);

        seed = seed * 1664525u + 1013904223u;
        const uint16_t y0 = (uint16_t)(seed % MATRIX_H);

        /* яркость и цвет */
        const uint8_t v0 = (uint8_t)(160u + (tri_u8((uint16_t)(phase + i * 41u)) >> 2)); /* 160..223 */
        const uint8_t hue = (uint8_t)(70u + i * 13u); /* зеленоватый диапазон */

        /* “пятно” 3x3 с затуханием */
        for (int8_t oy = -1; oy <= 1; oy++) {
            for (int8_t ox = -1; ox <= 1; ox++) {
                const int16_t xx = (int16_t)x0 + ox;
                const int16_t yy = (int16_t)y0 + oy;
                if (xx < 0 || yy < 0) continue;
                if ((uint16_t)xx >= MATRIX_W || (uint16_t)yy >= MATRIX_H) continue;

                const uint8_t att =
                    (ox == 0 && oy == 0) ? 255 :
                    ((ox == 0 || oy == 0) ? 140 : 90);

                const uint8_t v = (uint8_t)((uint16_t)v0 * att / 255u);

                uint8_t r, g, b;
                hsv_to_rgb_u8(hue, 255, v, &r, &g, &b);
                matrix_ws2812_set_pixel_xy((uint16_t)xx, (uint16_t)yy, r, g, b);
            }
        }
    }
}

/* ------------------------------------------------------------
 * FX #5 (ID 39): EQUALIZER BARS
 *   Псевдо-эквалайзер: высота столбиков от hash(x, phase).
 *   Цвет: градиент снизу-вверх.
 * ------------------------------------------------------------ */
void fx_equalizer_bars_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (4u * 100u / sp + 1u);

    const uint32_t seed = (uint32_t)(0x13579BDFu ^ (phase * 33u));

    for (uint16_t x = 0; x < MATRIX_W; x++) {
        /* 0..255 -> 0..MATRIX_H */
        const uint8_t h8 = hash2_u8(x, 0u, seed);
        uint16_t h = (uint16_t)((uint32_t)h8 * MATRIX_H / 255u);
        if (h > MATRIX_H) h = MATRIX_H;

        for (uint16_t y = 0; y < MATRIX_H; y++) {
            if (y >= (MATRIX_H - h)) {
                /* цвет по высоте */
                const uint8_t v = (uint8_t)(180u + (uint16_t)(y * 75u) / (MATRIX_H ? MATRIX_H : 1u));
                uint8_t r, g, b;
                hsv_to_rgb_u8((uint8_t)(phase + y * 10u), 255, v, &r, &g, &b);
                matrix_ws2812_set_pixel_xy(x, y, r, g, b);
            }
        }
    }
}
