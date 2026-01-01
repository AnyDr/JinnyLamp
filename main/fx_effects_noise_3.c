#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <stdint.h>
#include <stdbool.h>

/*
 * fx_effects_noise_3.c
 *
 * Пять эффектов “noise-style”, портируемых без FastLED:
 *   - CLOUDS (облака)
 *   - LAVA   (лава)
 *   - PLASMA (плазма)
 *   - FOREST (лес)
 *   - OCEAN  (океан)
 *
 * Инвариант проекта:
 *   - эффект НЕ вызывает show(), не трогает задачи/таймеры.
 *   - эффект только рисует кадр через matrix_ws2812_set_pixel_xy().
 *
 * Производительность:
 *   - 48x16 = 768 пикселей; 10 FPS нормально даже с “лёгким” value-noise.
 */

/* -------------------- маленькие утилиты -------------------- */

typedef struct { uint8_t r, g, b; } rgb_t;

static inline uint8_t u8_lerp(uint8_t a, uint8_t b, uint8_t t)
{
    /* a + (b-a)*t/255, без float */
    return (uint8_t)(a + (int16_t)((int16_t)(b - a) * (int16_t)t) / 255);
}

static inline rgb_t rgb_lerp(rgb_t a, rgb_t b, uint8_t t)
{
    rgb_t o;
    o.r = u8_lerp(a.r, b.r, t);
    o.g = u8_lerp(a.g, b.g, t);
    o.b = u8_lerp(a.b, b.b, t);
    return o;
}

static inline uint8_t u8_mul(uint8_t a, uint8_t b)
{
    return (uint8_t)(((uint16_t)a * (uint16_t)b) / 255u);
}

static inline uint8_t u8_qgamma(uint8_t v)
{
    /* “квазигамма”: v^2/255 для подчёркивания контраста */
    return (uint8_t)(((uint16_t)v * (uint16_t)v) / 255u);
}

/* -------------------- псевдо-рандом + value-noise -------------------- */

/* Быстрый хэш 32-bit -> 8-bit (детерминированный) */
static inline uint8_t hash8(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (uint8_t)(x & 0xFFu);
}

/* value noise на целочисленной сетке, bilinear, вход в формате 8.8 fixed */
static uint8_t noise2d_u8(uint16_t x_fp, uint16_t y_fp, uint32_t seed)
{
    const uint16_t xi = (uint16_t)(x_fp >> 8);
    const uint16_t yi = (uint16_t)(y_fp >> 8);
    const uint8_t  xf = (uint8_t)(x_fp & 0xFFu);
    const uint8_t  yf = (uint8_t)(y_fp & 0xFFu);

    const uint32_t k00 = seed ^ ((uint32_t)xi * 374761393u) ^ ((uint32_t)yi * 668265263u);
    const uint32_t k10 = seed ^ ((uint32_t)(xi + 1u) * 374761393u) ^ ((uint32_t)yi * 668265263u);
    const uint32_t k01 = seed ^ ((uint32_t)xi * 374761393u) ^ ((uint32_t)(yi + 1u) * 668265263u);
    const uint32_t k11 = seed ^ ((uint32_t)(xi + 1u) * 374761393u) ^ ((uint32_t)(yi + 1u) * 668265263u);

    const uint8_t n00 = hash8(k00);
    const uint8_t n10 = hash8(k10);
    const uint8_t n01 = hash8(k01);
    const uint8_t n11 = hash8(k11);

    const uint8_t nx0 = u8_lerp(n00, n10, xf);
    const uint8_t nx1 = u8_lerp(n01, n11, xf);
    return u8_lerp(nx0, nx1, yf);
}

/* Октавы: 2 слоя value-noise (дёшево, но живо) */
static uint8_t fbm2_u8(uint16_t x_fp, uint16_t y_fp, uint32_t seed)
{
    const uint8_t a = noise2d_u8(x_fp, y_fp, seed);
    const uint8_t b = noise2d_u8((uint16_t)(x_fp << 1), (uint16_t)(y_fp << 1), seed ^ 0xA5A5A5A5u);
    /* 0.65*a + 0.35*b примерно */
    return (uint8_t)(((uint16_t)a * 166u + (uint16_t)b * 89u) / 255u);
}

/* -------------------- 5 эффектов -------------------- */

/* 1) CLOUDS */
void fx_clouds_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    /* Движение: phase даёт нам “время” (у тебя оно уже масштабируется speed_pct внутри engine) */
    const uint32_t ph = ctx->phase;

    /* Цветовая растяжка: тёмно-синий -> серо-белый */
    const rgb_t c0 = (rgb_t){  5,  8, 20};
    const rgb_t c1 = (rgb_t){ 90,110,150};
    const rgb_t c2 = (rgb_t){220,235,255};

    /* Scale: чем больше, тем “мельче” */
    const uint16_t scale = 55u;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t xf = (uint16_t)((x * 256u) / scale);
            const uint16_t yf = (uint16_t)((y * 256u) / scale);

            const uint16_t xfp = (uint16_t)(xf + (uint16_t)(ph >> 9));
            const uint16_t yfp = (uint16_t)(yf + (uint16_t)(ph >> 10));

            uint8_t v = fbm2_u8(xfp, yfp, 0xC10ED5u); /* seed: любое валидное hex */
            v = u8_qgamma(v);

            rgb_t mid = rgb_lerp(c0, c1, v);
            rgb_t out = rgb_lerp(mid, c2, (uint8_t)(v >> 1));

            matrix_ws2812_set_pixel_xy(x, y, out.r, out.g, out.b);
        }
    }
}

/* 2) LAVA */
void fx_lava_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t ph = ctx->phase;

    const rgb_t cold = (rgb_t){  5,  0,  0};
    const rgb_t hot  = (rgb_t){255, 60,  0};
    const rgb_t fire = (rgb_t){255,200, 20};

    const uint16_t scale = 45u;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t xf = (uint16_t)((x * 256u) / scale);
            const uint16_t yf = (uint16_t)((y * 256u) / scale);

            const uint16_t xfp = (uint16_t)(xf + (uint16_t)(ph >> 8));
            const uint16_t yfp = (uint16_t)(yf + (uint16_t)(ph >> 10));

            const uint8_t base = fbm2_u8(xfp, yfp, 0x1A4A0A9u);
            const uint8_t crack = noise2d_u8((uint16_t)(xfp << 1), (uint16_t)(yfp << 1), 0xBADC0DEu);

            /* “трещины”: если crack низкий -> темно */
            if (crack < 42u) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
                continue;
            }

            rgb_t mid = rgb_lerp(cold, hot, base);
            rgb_t out = rgb_lerp(mid, fire, u8_qgamma(base));

            matrix_ws2812_set_pixel_xy(x, y, out.r, out.g, out.b);
        }
    }
}

/* 3) PLASMA */
void fx_plasma_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t ph = ctx->phase;
    const uint16_t scale = 38u;

    const uint16_t cx = (uint16_t)(MATRIX_W / 2u);
    const uint16_t cy = (uint16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t xf = (uint16_t)((x * 256u) / scale);
            const uint16_t yf = (uint16_t)((y * 256u) / scale);

            const uint16_t xfp = (uint16_t)(xf + (uint16_t)(ph >> 9));
            const uint16_t yfp = (uint16_t)(yf + (uint16_t)(ph >> 11));

            /* seed: валидная константа (детерминированная) */
            uint8_t n = fbm2_u8(xfp, yfp, 0x51A5AAu);


            /* Радиальный вклад */
            const uint16_t dx = (x > cx) ? (x - cx) : (cx - x);
            const uint16_t dy = (y > cy) ? (y - cy) : (cy - y);
            uint8_t d = (uint8_t)((dx * 11u + dy * 19u) & 0xFFu);

            uint8_t v = (uint8_t)(n ^ d);
            v = u8_qgamma(v);

            /* Фиолетовый -> голубой -> белый */
            const rgb_t a = (rgb_t){140,  0, 220};
            const rgb_t b = (rgb_t){ 30,160, 255};
            const rgb_t c = (rgb_t){255,255, 255};

            rgb_t mid = rgb_lerp(a, b, v);
            rgb_t out = rgb_lerp(mid, c, (uint8_t)(v >> 1));

            matrix_ws2812_set_pixel_xy(x, y, out.r, out.g, out.b);
        }
    }
}

/* 4) FOREST */
void fx_forest_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t ph = ctx->phase;
    const uint16_t scale = 70u;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t xf = (uint16_t)((x * 256u) / scale);
            const uint16_t yf = (uint16_t)((y * 256u) / scale);

            const uint16_t xfp = (uint16_t)(xf + (uint16_t)(ph >> 10));
            const uint16_t yfp = (uint16_t)(yf + (uint16_t)(ph >> 12));

            /* seed: валидная константа (детерминированная) */
            uint8_t n = fbm2_u8(xfp, yfp, 0xF0EE57u);


            /* Вертикальная “почва/крона” */
            const uint8_t vgrad = (uint8_t)((y * 255u) / (MATRIX_H - 1u));

            /* База зелёная, с жёлтыми “светлячками” */
            uint8_t g = (uint8_t)(40u + (n * 180u) / 255u);
            uint8_t r = (uint8_t)((n > 200u) ? (n - 200u) * 3u : 0u);
            uint8_t b = (uint8_t)((n < 80u) ? (80u - n) : 0u);

            /* Затемнение снизу */
            const uint8_t shade = (uint8_t)(255u - (vgrad / 2u));
            r = u8_mul(r, shade);
            g = u8_mul(g, shade);
            b = u8_mul(b, shade);

            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* 5) OCEAN */
void fx_ocean_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t ph = ctx->phase;
    const uint16_t scale = 60u;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t xf = (uint16_t)((x * 256u) / scale);
            const uint16_t yf = (uint16_t)((y * 256u) / scale);

            const uint16_t xfp = (uint16_t)(xf + (uint16_t)(ph >> 9));
            const uint16_t yfp = (uint16_t)(yf + (uint16_t)(ph >> 11));

            const uint8_t n1  = fbm2_u8(xfp, yfp, 0x0CEA11u);
            const uint8_t n2v = noise2d_u8((uint16_t)(xfp << 1), (uint16_t)(yfp << 1), 0xB10E55u);

            uint8_t wave = (uint8_t)((n1 / 2u) + (n2v / 2u));


            /* База океана */
            uint8_t r = (uint8_t)(0u);
            uint8_t g = (uint8_t)(20u + (wave * 100u) / 255u);
            uint8_t b = (uint8_t)(60u + (wave * 190u) / 255u);

            /* Белые “блики” на гребнях */
            if (wave > 210u) {
                const uint8_t w = (uint8_t)((wave - 210u) * 3u);
                r = u8_lerp(r, 220u, w);
                g = u8_lerp(g, 240u, w);
                b = u8_lerp(b, 255u, w);
            }

            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
