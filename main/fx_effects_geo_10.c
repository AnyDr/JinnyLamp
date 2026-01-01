#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * fx_effects_geo_10.c
 *
 * Пакет #10: эффекты ID 50..54
 * Требования:
 *   - без float
 *   - без больших буферов
 *   - каждый render перерисовывает весь кадр
 *   - скорость берём из ctx->speed_pct
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

static inline uint8_t boost_u8(uint8_t v)
{
    /* лёгкий “гамма-подобный” буст без LUT */
    return (uint8_t)(((uint16_t)v * (uint16_t)v) / 255u);
}

static inline uint16_t abs_u16(int16_t v)
{
    return (uint16_t)(v < 0 ? -v : v);
}

/* ------------------------------------------------------------
 * ID 50: AURORA BANDS
 *   Мягкие “ленты” по X, с вертикальной модуляцией и живостью от hash.
 * ------------------------------------------------------------ */
void fx_aurora_bands_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (3u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            /* базовая “волна” по X + сдвиг от Y */
            const uint16_t p = (uint16_t)(ph + x * 18u + y * 7u);
            uint8_t v = tri_u8(p);

            /* усиливаем центр “ленты” */
            v = (uint8_t)((uint16_t)boost_u8(v) * (200u + (tri_u8((uint16_t)(ph + y * 23u)) >> 2)) / 255u);

            /* небольшой шум, чтобы не было “стерильной” полосатости */
            v = (uint8_t)((uint16_t)v + (hash2_u8(x, y, 0xA0B0C001u ^ (uint32_t)ph) >> 5));
            if ((int)v > 255) v = 255;

            const uint8_t hue = (uint8_t)(110u + (ph >> 1) + (tri_u8((uint16_t)(ph + x * 9u)) >> 3)); /* зелено-сине */
            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * ID 51: RADIAL SWIRL
 *   “Вихрь” вокруг центра: манхэттен-радиус + фазовый перекос по dx/dy.
 * ------------------------------------------------------------ */
void fx_radial_swirl_render(fx_ctx_t *ctx, uint32_t t_ms)
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

            const uint16_t r = (uint16_t)(abs_u16(dx) + abs_u16(dy)); /* “радиус” ромбом */
            const uint16_t twist = (uint16_t)((dx * 11) ^ (dy * 17)); /* псевдо-угол */
            const uint8_t  w = tri_u8((uint16_t)(ph + r * 20u + twist));

            const uint8_t hue = (uint8_t)(ph + (twist >> 1) + r * 6u);
            const uint8_t val = (uint8_t)(40u + (w * 215u) / 255u);

            uint8_t r8,g8,b8;
            hsv_to_rgb_u8(hue, 255, val, &r8, &g8, &b8);
            matrix_ws2812_set_pixel_xy(x, y, r8, g8, b8);
        }
    }
}

/* ------------------------------------------------------------
 * ID 52: METEOR SHOWER
 *   Несколько “метеоров” с хвостом. Без хранения состояния:
 *   головы и скорость берутся из hash(phase, k).
 * ------------------------------------------------------------ */
void fx_meteor_shower_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (2u * 100u / sp + 1u);

    const uint8_t  meteors = 4;
    const uint16_t tail = 12;

    const uint32_t seed0 = (uint32_t)(0x1E7E0D52u ^ (ph >> 3));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            uint8_t best_v = 0;
            uint8_t best_h = 0;

            for (uint8_t k = 0; k < meteors; k++) {
                uint32_t s = xs32(seed0 ^ (uint32_t)k * 0x9E3779B1u);

                /* старт X + небольшой наклон */
                const uint16_t x0 = (uint16_t)(s % (uint32_t)MATRIX_W);
                s = xs32(s);
                const uint8_t  slope = (uint8_t)(1u + (s & 3u)); /* 1..4 */
                s = xs32(s);
                const uint8_t  hue0 = (uint8_t)(s & 0xFFu);

                /* голова “падает” вниз с фазой */
                const uint16_t hy = (uint16_t)((ph + (uint32_t)k * 37u) % (uint32_t)(MATRIX_H + tail));
                const uint16_t hx = (uint16_t)((x0 + (hy / (uint16_t)slope)) % MATRIX_W);

                /* расстояние манхэттен до головы */
                const uint16_t dx = (x > hx) ? (x - hx) : (hx - x);
                const uint16_t dy = (y > hy) ? (y - hy) : (hy - y);
                const uint16_t d  = (uint16_t)(dx + dy);

                if (d > tail) continue;

                uint8_t v = (uint8_t)(((uint32_t)(tail - d) * 255u) / tail);
                v = boost_u8(v);

                if (v > best_v) { best_v = v; best_h = (uint8_t)(hue0 + d * 6u); }
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
 * ID 53: KALEIDOSCOPE
 *   Симметрия по четвертям: берём “виртуальную точку” (mx,my),
 *   цвет и яркость считаем по hash + триангулятору.
 * ------------------------------------------------------------ */
void fx_kaleidoscope_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (4u * 100u / sp + 1u);

    const uint16_t hw = (uint16_t)(MATRIX_W / 2u);
    const uint16_t hh = (uint16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* зеркалим в “первый квадрант” */
            uint16_t mx = (x < hw) ? x : (uint16_t)(MATRIX_W - 1u - x);
            uint16_t my = (y < hh) ? y : (uint16_t)(MATRIX_H - 1u - y);

            const uint8_t n = hash2_u8(mx, my, 0x4A131D05u ^ (uint32_t)(ph >> 2));
            const uint8_t w = tri_u8((uint16_t)(ph + mx * 33u + my * 21u + n));

            const uint8_t hue = (uint8_t)(ph + n);
            const uint8_t val = (uint8_t)(40u + (w * 215u) / 255u);

            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, val, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * ID 54: RIPPLE FIELD
 *   “Ряби” от нескольких центров (центры от hash(phase,k)).
 *   Без буфера: берём max вклад по яркости.
 * ------------------------------------------------------------ */
void fx_ripple_field_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t ph = ctx->phase / (3u * 100u / sp + 1u);

    const uint8_t centers = 3;
    const uint32_t seed0 = (uint32_t)(0x71CC1354u ^ (ph >> 4));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            uint8_t best_v = 0;
            uint8_t best_h = 0;

            for (uint8_t k = 0; k < centers; k++) {
                uint32_t s = xs32(seed0 ^ (uint32_t)k * 0x85EBCA77u);

                const uint16_t cx = (uint16_t)(s % (uint32_t)MATRIX_W);
                s = xs32(s);
                const uint16_t cy = (uint16_t)(s % (uint32_t)MATRIX_H);
                s = xs32(s);
                const uint8_t  hue0 = (uint8_t)(s & 0xFFu);

                const uint16_t dx = (x > cx) ? (x - cx) : (cx - x);
                const uint16_t dy = (y > cy) ? (y - cy) : (cy - y);
                const uint16_t d  = (uint16_t)(dx + dy); /* ромб-радиус */

                /* волна: яркость зависит от (phase - d*step) */
                const uint8_t w = tri_u8((uint16_t)(ph * 4u - d * 28u));
                uint8_t v = (uint8_t)(w > 200 ? w : (uint8_t)(w >> 1)); /* подчёркиваем “кольца” */

                if (v > best_v) {
                    best_v = v;
                    best_h = (uint8_t)(hue0 + d * 7u);
                }
            }

            if (best_v == 0) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
            } else {
                best_v = (uint8_t)((uint16_t)best_v * 220u / 255u);
                uint8_t r,g,b;
                hsv_to_rgb_u8(best_h, 255, best_v, &r, &g, &b);
                matrix_ws2812_set_pixel_xy(x, y, r, g, b);
            }
        }
    }
}
