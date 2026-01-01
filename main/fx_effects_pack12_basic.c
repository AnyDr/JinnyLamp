#include "fx_engine.h"
#include "matrix_ws2812.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Пакет 12: базовые эффекты из “оригинальной” прошивки (cheap)
 * 60 WHITE_COLOR
 * 61 COLOR
 * 62 COLORS
 * 63 MADNESS
 * 64 BBALLS (упрощённо)
 * ============================================================ */

static inline uint8_t u8_clamp_i32(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* Быстрый HSV->RGB (h:0..255, s:0..255, v:0..255), без float */
static void hsv_u8(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = v; *g = v; *b = v; return; }

    const uint8_t region = (uint8_t)(h / 43);          // 0..5
    const uint8_t rem    = (uint8_t)((h - region * 43) * 6);

    const uint16_t p = (uint16_t)v * (255 - s) / 255;
    const uint16_t q = (uint16_t)v * (255 - (uint16_t)s * rem / 255) / 255;
    const uint16_t t = (uint16_t)v * (255 - (uint16_t)s * (255 - rem) / 255) / 255;

    switch (region) {
        default:
        case 0: *r = v; *g = (uint8_t)t; *b = (uint8_t)p; break;
        case 1: *r = (uint8_t)q; *g = v; *b = (uint8_t)p; break;
        case 2: *r = (uint8_t)p; *g = v; *b = (uint8_t)t; break;
        case 3: *r = (uint8_t)p; *g = (uint8_t)q; *b = v; break;
        case 4: *r = (uint8_t)t; *g = (uint8_t)p; *b = v; break;
        case 5: *r = v; *g = (uint8_t)p; *b = (uint8_t)q; break;
    }
}

static uint32_t xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* ------------------------------------------------------------
 * 60) WHITE_COLOR
 * ------------------------------------------------------------ */
void fx_white_color_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)ctx; (void)t_ms;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, 255, 255, 255);
        }
    }
}

/* ------------------------------------------------------------
 * 61) COLOR (один цвет, плавный hue drift)
 * ------------------------------------------------------------ */
void fx_color_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    /* hue дрейфует от phase: скорость задаётся движком через ctx->phase/base_step */
    const uint8_t hue = (uint8_t)(ctx->phase >> 8);

    uint8_t r,g,b;
    hsv_u8(hue, 255, 255, &r, &g, &b);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * 62) COLORS (смена цвета “ступеньками”, как в простых лампах)
 * ------------------------------------------------------------ */
void fx_colors_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    /* шаг по цветам: меняем hue не каждый кадр, а редко */
    const uint32_t step = (ctx->phase >> 16);      // медленнее
    const uint8_t hue = (uint8_t)(step * 9);       // крупные скачки

    uint8_t r,g,b;
    hsv_u8(hue, 255, 255, &r, &g, &b);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * 63) MADNESS (дешёвый “noise” без Perlin: зерно+цвет)
 * ------------------------------------------------------------ */
void fx_madness_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    /* seed меняется со временем, но стабилен в кадре */
    uint32_t s = (uint32_t)(0xA53C9E1Du ^ ctx->phase ^ (t_ms * 2654435761u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            s = xorshift32(s);

            /* яркость и hue из разных битов */
            const uint8_t hue = (uint8_t)(s >> 24);
            const uint8_t v   = (uint8_t)(80 + ((s >> 8) & 0x7F));   // 80..207

            uint8_t r,g,b;
            hsv_u8(hue, 255, v, &r, &g, &b);

            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ------------------------------------------------------------
 * 64) BBALLS (упрощённо): N точек, отражение от границ
 * без float, без тяжёлых хвостов
 * ------------------------------------------------------------ */
void fx_bballs_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    enum { N = 6 };

    /* статическое состояние на эффект (ok, пока один экземпляр эффекта в рантайме) */
    static uint8_t inited = 0;
    static int16_t x[N], y[N];
    static int8_t  vx[N], vy[N];
    static uint8_t hue0[N];

    if (!inited) {
        inited = 1;
        uint32_t s = (uint32_t)(0xC001D00Du ^ ctx->phase);
        for (int i = 0; i < N; i++) {
            s = xorshift32(s);
            x[i] = (int16_t)(s % MATRIX_W);
            s = xorshift32(s);
            y[i] = (int16_t)(s % MATRIX_H);

            s = xorshift32(s);
            vx[i] = (int8_t)((s & 3u) + 1);     // 1..4
            s = xorshift32(s);
            vy[i] = (int8_t)(((s & 3u) + 1));   // 1..4

            s = xorshift32(s);
            hue0[i] = (uint8_t)(s >> 24);
        }
    }

    /* фон слегка затемняем через “не рисовать всё чёрным” нельзя,
     * поэтому просто рисуем чёрный фон кадра */
    for (uint16_t yy = 0; yy < MATRIX_H; yy++) {
        for (uint16_t xx = 0; xx < MATRIX_W; xx++) {
            matrix_ws2812_set_pixel_xy(xx, yy, 0, 0, 0);
        }
    }

    /* step из phase: чем выше speed/base_step, тем быстрее phase, тем быстрее движение */
    const int16_t step = 1 + (int16_t)((ctx->phase >> 24) & 1u);  // 1..2

    for (int i = 0; i < N; i++) {
        x[i] = (int16_t)(x[i] + vx[i] * step);
        y[i] = (int16_t)(y[i] + vy[i] * step);

        if (x[i] < 0) { x[i] = 0; vx[i] = (int8_t)(-vx[i]); }
        if (y[i] < 0) { y[i] = 0; vy[i] = (int8_t)(-vy[i]); }

        if (x[i] >= (int16_t)MATRIX_W) { x[i] = (int16_t)(MATRIX_W - 1); vx[i] = (int8_t)(-vx[i]); }
        if (y[i] >= (int16_t)MATRIX_H) { y[i] = (int16_t)(MATRIX_H - 1); vy[i] = (int8_t)(-vy[i]); }

        uint8_t r,g,b;
        hsv_u8((uint8_t)(hue0[i] + (ctx->phase >> 10)), 255, 255, &r, &g, &b);
        matrix_ws2812_set_pixel_xy((uint16_t)x[i], (uint16_t)y[i], r, g, b);
    }
}
