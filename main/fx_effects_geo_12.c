#include "fx_engine.h"
#include "matrix_ws2812.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 *  Пакет 13: 5 эффектов (геометрия/псевдо-3D/поле)
 *  Инвариант: каждый render полностью перерисовывает кадр.
 * ============================================================ */

static inline uint8_t clamp_u8_i32(int32_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void fill_black(void)
{
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
        }
    }
}

/* Простой HSV->RGB (h:0..255, s:0..255, v:0..255) */
static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) {
        *r = v; *g = v; *b = v;
        return;
    }

    const uint8_t region = (uint8_t)(h / 43);          // 0..5
    const uint8_t rem    = (uint8_t)((h - region * 43) * 6);

    const uint16_t p = (uint16_t)v * (uint16_t)(255 - s) / 255;
    const uint16_t q = (uint16_t)v * (uint16_t)(255 - ((uint16_t)s * rem) / 255) / 255;
    const uint16_t t = (uint16_t)v * (uint16_t)(255 - ((uint16_t)s * (255 - rem)) / 255) / 255;

    switch (region) {
        default:
        case 0: *r = v;   *g = (uint8_t)t; *b = (uint8_t)p; break;
        case 1: *r = (uint8_t)q; *g = v;   *b = (uint8_t)p; break;
        case 2: *r = (uint8_t)p; *g = v;   *b = (uint8_t)t; break;
        case 3: *r = (uint8_t)p; *g = (uint8_t)q; *b = v;   break;
        case 4: *r = (uint8_t)t; *g = (uint8_t)p; *b = v;   break;
        case 5: *r = v;   *g = (uint8_t)p; *b = (uint8_t)q; break;
    }
}

/* ============================================================
 *  65) DNA (две “спирали” + мостики)
 * ============================================================ */
void fx_dna_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;
    fill_black();

    const float t = (float)(ctx->phase & 0xFFFFu) * 0.0025f;
    const float amp = 10.0f;        // ширина “разлёта” спиралей
    const float freq = 0.55f;       // частота по Y

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        const float yf = (float)y;
        const float s = sinf(yf * freq + t);

        int x1 = (int)((int)MATRIX_W / 2 - 6 + (int)(amp * s));
        int x2 = (int)((int)MATRIX_W / 2 + 6 - (int)(amp * s));

        if (x1 < 0) x1 = 0;
        if (x1 >= (int)MATRIX_W) x1 = (int)MATRIX_W - 1;
        if (x2 < 0) x2 = 0;
        if (x2 >= (int)MATRIX_W) x2 = (int)MATRIX_W - 1;

        uint8_t r1,g1,b1,r2,g2,b2;
        hsv_to_rgb((uint8_t)((y * 12 + (ctx->phase >> 8)) & 0xFF), 255, 220, &r1,&g1,&b1);
        hsv_to_rgb((uint8_t)((y * 12 + 128 + (ctx->phase >> 8)) & 0xFF), 255, 220, &r2,&g2,&b2);

        matrix_ws2812_set_pixel_xy((uint16_t)x1, y, r1, g1, b1);
        matrix_ws2812_set_pixel_xy((uint16_t)x2, y, r2, g2, b2);

        /* “мостики” между нитями через одну строку */
        if ((y & 1u) == 0u) {
            const int lo = (x1 < x2) ? x1 : x2;
            const int hi = (x1 < x2) ? x2 : x1;
            const int mid = (lo + hi) / 2;
            matrix_ws2812_set_pixel_xy((uint16_t)mid, y, 40, 40, 40);
        }
    }
}

/* ============================================================
 *  66) METABALLS (поле от 3 шаров)
 * ============================================================ */
void fx_metaballs_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t p = ctx->phase;
    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;

    /* Три “шара” двигаются по Lissajous (минимум тригонометрии) */
    const float t = (float)(p & 0xFFFFu) * 0.0020f;

    const float cx1 = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 1.10f));
    const float cy1 = (float)(h - 1) * (0.5f + 0.35f * sinf(t * 1.40f));

    const float cx2 = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 0.90f + 1.7f));
    const float cy2 = (float)(h - 1) * (0.5f + 0.35f * sinf(t * 1.60f + 0.9f));

    const float cx3 = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 1.30f + 2.4f));
    const float cy3 = (float)(h - 1) * (0.5f + 0.35f * sinf(t * 1.05f + 1.2f));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float dx1 = (float)x - cx1; const float dy1 = (float)y - cy1;
            const float dx2 = (float)x - cx2; const float dy2 = (float)y - cy2;
            const float dx3 = (float)x - cx3; const float dy3 = (float)y - cy3;

            /* поле: сумма вкладов 1/(d^2+eps) */
            const float f =
                70.0f / (dx1*dx1 + dy1*dy1 + 4.0f) +
                70.0f / (dx2*dx2 + dy2*dy2 + 4.0f) +
                70.0f / (dx3*dx3 + dy3*dy3 + 4.0f);

            uint8_t r,g,b;
            const int32_t v = (int32_t)(f * 18.0f); // усиление под нашу матрицу
            if (v < 40) {
                r = g = b = 0;
            } else {
                hsv_to_rgb((uint8_t)((ctx->phase >> 8) + (uint8_t)(x * 3 + y * 9)), 255,
                           clamp_u8_i32(v), &r,&g,&b);
            }
            matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y, r,g,b);
        }
    }
}

/* ============================================================
 *  67) LAVA LAMP (вертикальные “пузыри”)
 * ============================================================ */
void fx_lava_lamp_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;

    const float t = (float)(ctx->phase & 0xFFFFu) * 0.0022f;

    /* 4 пузыря: x гуляет синусом, y медленно “плывёт” */
    float bx[4], by[4];
    bx[0] = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 0.8f + 0.1f));
    by[0] = (float)(h - 1) * (0.5f + 0.40f * sinf(t * 0.5f + 1.1f));

    bx[1] = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 1.1f + 2.0f));
    by[1] = (float)(h - 1) * (0.5f + 0.40f * sinf(t * 0.6f + 2.2f));

    bx[2] = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 0.9f + 3.0f));
    by[2] = (float)(h - 1) * (0.5f + 0.40f * sinf(t * 0.55f + 0.7f));

    bx[3] = (float)(w - 1) * (0.5f + 0.35f * sinf(t * 1.3f + 4.0f));
    by[3] = (float)(h - 1) * (0.5f + 0.40f * sinf(t * 0.45f + 1.9f));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float f = 0.0f;
            for (int i = 0; i < 4; i++) {
                const float dx = (float)x - bx[i];
                const float dy = (float)y - by[i];
                f += 90.0f / (dx*dx + dy*dy + 6.0f);
            }

            /* “лава”: тёплая палитра */
            const int32_t v = (int32_t)(f * 14.0f);
            uint8_t r,g,b;
            if (v < 30) {
                r = g = b = 0;
            } else {
                /* h: от красного к жёлтому */
                const uint8_t h0 = (uint8_t)(8u + (uint8_t)((v >> 2) & 31u));
                hsv_to_rgb(h0, 255, clamp_u8_i32(v), &r,&g,&b);
            }
            matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y, r,g,b);
        }
    }
}

/* ============================================================
 *  68) PRISM (вращающийся “угловой” градиент)
 * ============================================================ */
void fx_prism_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;

    const float t = (float)(ctx->phase & 0xFFFFu) * 0.0018f;
    const float cs = cosf(t);
    const float sn = sinf(t);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float xf = (float)x - (float)(w - 1) * 0.5f;
            const float yf = (float)y - (float)(h - 1) * 0.5f;

            /* поворачиваем координаты */
            const float u = xf * cs - yf * sn;
            const float v = xf * sn + yf * cs;

            /* h зависит от угла, яркость от расстояния */
            const float rr = sqrtf(u*u + v*v);
            uint8_t r,g,b;
            const uint8_t hue = (uint8_t)((int)(u * 6.0f + v * 10.0f + (float)(ctx->phase >> 8)) & 0xFF);
            const int32_t val = 255 - (int32_t)(rr * 18.0f);
            hsv_to_rgb(hue, 255, clamp_u8_i32(val), &r,&g,&b);
            matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y, r,g,b);
        }
    }
}

/* ============================================================
 *  69) CUBES (псевдо-3D “квадраты в квадрате”)
 * ============================================================ */
void fx_cubes_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;
    fill_black();

    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;
    const int cx = w / 2;
    const int cy = h / 2;

    /* 4 “квадрата”, масштаб пульсирует */
    const float t = (float)(ctx->phase & 0xFFFFu) * 0.0020f;

    for (int k = 0; k < 4; k++) {
        const float pulse = 0.55f + 0.45f * sinf(t + (float)k * 0.9f);
        int hw = (int)( (10 + k * 6) * pulse );
        int hh = (int)( (5  + k * 3) * pulse );

        if (hw < 2) hw = 2;
        if (hh < 2) hh = 2;

        const uint8_t hue = (uint8_t)((ctx->phase >> 8) + (uint8_t)(k * 50));
        uint8_t r,g,b;
        hsv_to_rgb(hue, 255, 200, &r,&g,&b);

        /* рисуем “рамку” квадрата */
        for (int x = cx - hw; x <= cx + hw; x++) {
            const int y1 = cy - hh;
            const int y2 = cy + hh;
            if (x >= 0 && x < w) {
                if (y1 >= 0 && y1 < h) matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y1, r,g,b);
                if (y2 >= 0 && y2 < h) matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y2, r,g,b);
            }
        }
        for (int y = cy - hh; y <= cy + hh; y++) {
            const int x1 = cx - hw;
            const int x2 = cx + hw;
            if (y >= 0 && y < h) {
                if (x1 >= 0 && x1 < w) matrix_ws2812_set_pixel_xy((uint16_t)x1, (uint16_t)y, r,g,b);
                if (x2 >= 0 && x2 < w) matrix_ws2812_set_pixel_xy((uint16_t)x2, (uint16_t)y, r,g,b);
            }
        }
    }

    /* лёгкий “искрящийся” шум по углам */
    uint32_t seed = 0xA53C91u ^ (ctx->phase * 2654435761u);
    for (int i = 0; i < 12; i++) {
        const uint32_t r = xorshift32(&seed);
        const uint16_t x = (uint16_t)(r % MATRIX_W);
        const uint16_t y = (uint16_t)((r >> 8) % MATRIX_H);
        matrix_ws2812_set_pixel_xy(x, y, 40, 40, 40);
    }
}
