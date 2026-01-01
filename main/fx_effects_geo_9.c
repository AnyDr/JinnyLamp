#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_9.c
 *
 * Пакет #9: эффекты ID 45..49
 * Цели:
 *   - без float
 *   - без больших буферов
 *   - каждый render полностью перерисовывает кадр (нет “мусора” в буфере)
 *   - скорость контролируется через ctx->speed_pct
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

static inline uint16_t abs16(int16_t v)
{
    return (uint16_t)(v < 0 ? -v : v);
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

/* лёгкий “boost” для яркости без LUT */
static inline uint8_t boost_u8(uint8_t v)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)v) / 255u);
}

/* ------------------------------------------------------------
 * ID 45: DIAMOND RINGS
 *   “Кольца” по манхэттен-дистанции (ромбы), цвет бежит по радиусу.
 * ------------------------------------------------------------ */
void fx_diamond_rings_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (4u * 100u / sp + 1u);

    const int16_t cx = (int16_t)(MATRIX_W / 2u);
    const int16_t cy = (int16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const int16_t dx = (int16_t)x - cx;
            const int16_t dy = (int16_t)y - cy;

            const uint16_t dist = (uint16_t)(abs16(dx) + abs16(dy)); /* ромб */
            const uint8_t w = tri_u8((uint16_t)(ph + dist * 22u));   /* 0..255..0 */

            /* цвет зависит от радиуса + времени */
            const uint8_t hue = (uint8_t)(ph + dist * 9u);
            const uint8_t val = (uint8_t)(80u + (w >> 1));          /* 80..207 */

            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, val, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * ID 46: COMET DIAGONAL
 *   Комета с хвостом (без буфера): яркость зависит от manhattan до головы.
 * ------------------------------------------------------------ */
void fx_comet_diagonal_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (3u * 100u / sp + 1u);

    /* голова бежит по диагонали */
    const uint16_t hx = (uint16_t)(ph % (uint32_t)MATRIX_W);
    const uint16_t hy = (uint16_t)((ph * 3u) % (uint32_t)MATRIX_H);

    const uint16_t tail = 10; /* длина хвоста (пикс) */
    const uint8_t  hue0 = (uint8_t)(ph * 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            const uint16_t dx = (x > hx) ? (x - hx) : (hx - x);
            const uint16_t dy = (y > hy) ? (y - hy) : (hy - y);
            const uint16_t d  = (uint16_t)(dx + dy);

            if (d > tail) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
                continue;
            }

            /* хвост: ближе к голове ярче */
            uint8_t v = (uint8_t)((uint32_t)(tail - d) * 255u / tail);
            v = boost_u8(v);

            uint8_t r,g,b;
            hsv_to_rgb_u8((uint8_t)(hue0 + d * 6u), 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * ID 47: FIREWORKS (BURSTS)
 *   Несколько “взрывов” без хранения состояния:
 *   центры и фаза берутся из hash(t, k).
 * ------------------------------------------------------------ */
void fx_fireworks_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (2u * 100u / sp + 1u);

    /* “кадры” взрывов */
    const uint32_t slice = ph >> 5;         /* меняем набор центров примерно каждые 32 тика */
    const uint8_t  age   = (uint8_t)(ph & 0xFFu); /* 0..255 */

    const uint8_t  k_bursts = 3;
    const uint32_t seed0 = (uint32_t)(0xA5B3571Du ^ (slice * 0x9E3779B1u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            uint8_t best_v = 0;
            uint8_t best_h = 0;

            for (uint8_t k = 0; k < k_bursts; k++) {
                uint32_t s = xs32(seed0 ^ (uint32_t)k * 0x68E31DA4u);

                const uint16_t cx = (uint16_t)(s % (uint32_t)MATRIX_W);
                s = xs32(s);
                const uint16_t cy = (uint16_t)(s % (uint32_t)MATRIX_H);

                /* радиус взрыва растёт */
                const uint8_t  r0 = (uint8_t)(age >> 4); /* 0..15 */
                const uint16_t dx = (x > cx) ? (x - cx) : (cx - x);
                const uint16_t dy = (y > cy) ? (y - cy) : (cy - y);
                const uint16_t d  = (uint16_t)(dx + dy);

                /* кольцо толщиной ~2 */
                if (d < r0 || d > (uint16_t)(r0 + 2u)) {
                    continue;
                }

                /* яркость зависит от возраста: пик в середине */
                uint8_t v = tri_u8((uint16_t)(age * 2u));
                v = (uint8_t)((uint16_t)v * 220u / 255u);

                if (v > best_v) {
                    best_v = v;
                    best_h = (uint8_t)(s ^ age); /* “цвет” взрыва */
                }
            }

            if (best_v == 0) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
            } else {
                uint8_t r,g,b;
                hsv_to_rgb_u8(best_h, 255, best_v, &r, &g, &b);
                matrix_ws2812_set_pixel_xy(x, y, r, g, b);
            }
        }
    }
}

/* ------------------------------------------------------------
 * ID 48: SCANLINES
 *   Горизонтальная “скан-линия” бежит по Y, с мягкими краями.
 * ------------------------------------------------------------ */
void fx_scanlines_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (3u * 100u / sp + 1u);

    const uint16_t ly = (uint16_t)(ph % (uint32_t)MATRIX_H);
    const uint8_t  hue = (uint8_t)(ph * 3u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        const uint16_t dy = (y > ly) ? (y - ly) : (ly - y);

        /* мягкие края: 0 -> ярко, 1 -> средне, 2+ -> темно */
        uint8_t v = 0;
        if (dy == 0) v = 255;
        else if (dy == 1) v = 120;
        else if (dy == 2) v = 40;
        else v = 0;

        for (uint16_t x = 0; x < MATRIX_W; x++) {
            if (v == 0) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
            } else {
                /* лёгкая модуляция по X */
                const uint8_t vv = (uint8_t)((uint16_t)v * (180u + (tri_u8((uint16_t)(ph + x * 17u)) >> 3)) / 255u);
                uint8_t r,g,b;
                hsv_to_rgb_u8((uint8_t)(hue + x * 2u), 255, vv, &r, &g, &b);
                matrix_ws2812_set_pixel_xy(x, y, r, g, b);
            }
        }
    }
}

/* ------------------------------------------------------------
 * ID 49: CHECKER SHIFT
 *   Шахматка: клетки дышат и меняют цвет, фаза смещает рисунок.
 * ------------------------------------------------------------ */
void fx_checker_shift_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (4u * 100u / sp + 1u);

    const uint8_t hue = (uint8_t)(ph * 2u);
    const uint8_t base = (uint8_t)(60u + (tri_u8((uint16_t)ph) >> 1)); /* 60..187 */

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* шахматка с бегущим смещением */
            const uint8_t cell = (uint8_t)(((x + (ph >> 3)) ^ (y + (ph >> 4))) & 1u);

            uint8_t v = cell ? base : (uint8_t)(base >> 2);
            /* микро-шум, чтобы “жило” */
            v = (uint8_t)((uint16_t)v + (hash2_u8(x, y, 0x1234ABCDu ^ ph) >> 5));

            uint8_t r,g,b;
            hsv_to_rgb_u8((uint8_t)(hue + cell * 120u), 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
