#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_6.c
 *
 * Пакет #6: ещё 5 лёгких эффектов (ID 30..34)
 *  - без float
 *  - без больших буферов/массивов
 *  - render() только заполняет буфер матрицы
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

/* простенький 2D hash -> 8-bit (детерминированный псевдошум) */
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

/* Быстрый HSV->RGB (0..255), без float */
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
 * FX #1: V-SCANNER (вертикальный сканер с хвостом)
 * ------------------------------------------------------------ */
void fx_v_scanner_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (2u * 100u / sp + 1u);

    const uint16_t x0 = (uint16_t)(phase % MATRIX_W);
    const uint8_t  tail = 10;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint8_t k = 0; k <= tail; k++) {
            const int16_t xx = (int16_t)x0 - (int16_t)k;
            if (xx < 0) continue;
            if ((uint16_t)xx >= MATRIX_W) continue;

            const uint8_t v = (uint8_t)(255u - (uint16_t)k * (255u / (tail + 1u)));

            /* циановый сканер */
            matrix_ws2812_set_pixel_xy((uint16_t)xx, y, 0, v, v);
        }
    }
}

/* ------------------------------------------------------------
 * FX #2: H-SCANNER (горизонтальный сканер с хвостом)
 * ------------------------------------------------------------ */
void fx_h_scanner_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    const uint16_t y0 = (uint16_t)(phase % MATRIX_H);
    const uint8_t  tail = 8;

    for (uint16_t x = 0; x < MATRIX_W; x++) {
        for (uint8_t k = 0; k <= tail; k++) {
            const int16_t yy = (int16_t)y0 - (int16_t)k;
            if (yy < 0) continue;
            if ((uint16_t)yy >= MATRIX_H) continue;

            const uint8_t v = (uint8_t)(255u - (uint16_t)k * (255u / (tail + 1u)));

            /* фиолетовый сканер */
            matrix_ws2812_set_pixel_xy(x, (uint16_t)yy, v, 0, v);
        }
    }
}

/* ------------------------------------------------------------
 * FX #3: TWINKLE STARS (мерцание "звёзд" на чёрном фоне)
 *   Без состояния: яркость каждого пикселя = tri(hash + phase)
 * ------------------------------------------------------------ */
void fx_twinkle_stars_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    matrix_ws2812_clear();

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (6u * 100u / sp + 1u);

    const uint32_t seed = (uint32_t)(0xA53C9E1Bu ^ (phase * 33u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* редкая "звезда": маской выбиваем большинство пикселей */
            const uint8_t h = hash2_u8(x, y, seed);
            if ((h & 0x1Fu) != 0) continue; /* примерно 1/32 пикселей */

            /* мерцание */
            const uint8_t v = tri_u8((uint16_t)(phase + (uint16_t)h * 2u));

            /* бело-голубой */
            matrix_ws2812_set_pixel_xy(x, y, v, v, (uint8_t)((uint16_t)v * 3u / 4u));
        }
    }
}

/* ------------------------------------------------------------
 * FX #4: COLOR WAVES (две пересекающиеся "волны" tri() по X и Y)
 *   Смотрится как синус-поля, но без float.
 * ------------------------------------------------------------ */
void fx_color_waves_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (4u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        const uint8_t vy = tri_u8((uint16_t)(phase + y * 28u));
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t vx = tri_u8((uint16_t)(phase + x * 12u));
            const uint8_t v  = (uint8_t)(((uint16_t)vx + (uint16_t)vy) / 2u);

            uint8_t r, g, b;
            hsv_to_rgb_u8((uint8_t)(phase + x * 3u), 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * FX #5: HEAT MAP (псевдо "тепловая" карта)
 *   Градиент: чёрный -> красный -> жёлтый -> белый
 *   Источник тепла гуляет по X, плюс “шум” по Y.
 * ------------------------------------------------------------ */
static inline void heat_to_rgb(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    /* h: 0..255 */
    if (h < 85) {
        *r = (uint8_t)(h * 3u);
        *g = 0;
        *b = 0;
    } else if (h < 170) {
        uint8_t t = (uint8_t)((h - 85u) * 3u);
        *r = 255;
        *g = t;
        *b = 0;
    } else {
        uint8_t t = (uint8_t)((h - 170u) * 3u);
        *r = 255;
        *g = 255;
        *b = t;
    }
}

void fx_heat_map_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (5u * 100u / sp + 1u);

    const uint16_t hot_x = (uint16_t)(phase % MATRIX_W);
    const uint32_t seed  = (uint32_t)(0xC001D00Du ^ (phase * 11u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* “температура” зависит от расстояния до hot_x (без abs для unsigned) */
            uint16_t dx = (x > hot_x) ? (x - hot_x) : (hot_x - x);
            if (dx > 60) dx = 60;

            /* базовый профиль */
            uint16_t base = (uint16_t)(255u - (dx * 255u) / 60u);

            /* добавим лёгкий “шум” по координатам */
            const uint8_t n = hash2_u8(x, y, seed);
            base = (uint16_t)((base * 3u + (uint16_t)n) / 4u);

            /* и чуть “дыхания” по Y */
            const uint8_t by = tri_u8((uint16_t)(phase + y * 21u));
            base = (uint16_t)((base * 3u + by) / 4u);

            uint8_t r, g, b;
            heat_to_rgb((uint8_t)base, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
