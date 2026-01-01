#include <stdint.h>
#include <stdbool.h>
#include <math.h> // sinf, cosf, sqrtf

#include "fx_engine.h"
#include "matrix_ws2812.h"

/* ============================================================
 * fx_effects_geo_14.c
 *
 * Назначение:
 *   Пакет геометрических эффектов #14 (5 шт).
 *   Реализованы под нашу архитектуру:
 *     - рендер в буфер через matrix_ws2812_set_pixel_xy()
 *     - один кадр = полная перерисовка всех пикселей
 *     - скорость управляется через ctx->speed_pct (10..300)
 *
 * Важно:
 *   - Никаких динамических аллокаций.
 *   - Все эффекты детерминированы и не требуют внешних очередей/ISR.
 * ============================================================ */

/* -------------------- маленькие хелперы -------------------- */

static inline uint8_t clamp_u8_i32(int32_t v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline uint8_t tri8(uint8_t x)
{
    /* треугольная волна 0..255..0 */
    return (x & 0x80) ? (uint8_t)(255 - ((x & 0x7F) << 1)) : (uint8_t)((x & 0x7F) << 1);
}

static inline uint32_t xorshift32(uint32_t *s)
{
    /* маленький PRNG для “структурного” шума/вариаций */
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* HSV(0..255) -> RGB(0..255), целочисленно, без таблиц */
static void hsv_to_rgb_u8(uint8_t h, uint8_t s, uint8_t v,
                          uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = v; *g = v; *b = v; return; }

    const uint8_t region = (uint8_t)(h / 43);            // 0..5
    const uint8_t rem    = (uint8_t)((h - region * 43) * 6);

    const uint16_t p = (uint16_t)v * (uint16_t)(255 - s) / 255;
    const uint16_t q = (uint16_t)v * (uint16_t)(255 - ((uint16_t)s * rem) / 255) / 255;
    const uint16_t t = (uint16_t)v * (uint16_t)(255 - ((uint16_t)s * (255 - rem)) / 255) / 255;

    switch (region) {
        default:
        case 0: *r = v;         *g = (uint8_t)t; *b = (uint8_t)p; break;
        case 1: *r = (uint8_t)q;*g = v;          *b = (uint8_t)p; break;
        case 2: *r = (uint8_t)p;*g = v;          *b = (uint8_t)t; break;
        case 3: *r = (uint8_t)p;*g = (uint8_t)q; *b = v;          break;
        case 4: *r = (uint8_t)t;*g = (uint8_t)p; *b = v;          break;
        case 5: *r = v;         *g = (uint8_t)p; *b = (uint8_t)q; break;
    }
}

/* Нормализация времени под speed_pct:
 * speed_pct=100 => t = t_ms
 * speed_pct=200 => быстрее (t*2)
 * speed_pct=50  => медленнее (t/2)
 */
static inline uint32_t t_scaled_ms(const fx_ctx_t *ctx, uint32_t t_ms)
{
    return (uint32_t)(((uint64_t)t_ms * (uint64_t)ctx->speed_pct) / 100ULL);
}


#if 0
/* -------------------- ЭФФЕКТ #69: CUBES -------------------- */
/*
 * Идея:
 *   - плитка “кубиков” (изометрические грани) по сетке
 *   - лёгкая анимация “высоты” по времени
 *   - цвет по индексу тайла + время
 */
void fx_cubes_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = t_scaled_ms(ctx, t_ms);

    const int cell = 8; // размер тайла (пикс)
    const uint8_t sat = 220;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const int tx = (int)(x / cell);
            const int ty = (int)(y / cell);

            const int lx = (int)(x % cell);
            const int ly = (int)(y % cell);

            /* “высота” тайла треугольной волной */
            const uint8_t htri = tri8((uint8_t)((tx * 29 + ty * 57 + (t >> 4)) & 0xFF));

            /* 3 грани: верх/левая/правая (простая геометрия в тайле 8x8) */
            bool top   = (ly < (cell/2)) && (lx >= (cell/2 - ly)) && (lx < (cell/2 + ly));
            bool left  = (lx < cell/2) && (ly >= lx) && (ly < cell - lx);
            bool right = (lx >= cell/2) && (ly >= (cell - 1 - lx)) && (ly < (lx + 1));

            uint8_t hue = (uint8_t)((tx * 17 + ty * 23 + (t >> 5)) & 0xFF);

            uint8_t r=0,g=0,b=0;

            if (top) {
                /* верх ярче */
                uint8_t v = (uint8_t)((180 + (htri >> 2)) & 0xFF);
                hsv_to_rgb_u8(hue, sat, v, &r, &g, &b);
            } else if (left) {
                /* левая грань темнее */
                uint8_t v = (uint8_t)((120 + (htri >> 3)) & 0xFF);
                hsv_to_rgb_u8(hue, sat, v, &r, &g, &b);
            } else if (right) {
                /* правая средняя */
                uint8_t v = (uint8_t)((150 + (htri >> 3)) & 0xFF);
                hsv_to_rgb_u8(hue, sat, v, &r, &g, &b);
            } else {
                /* фон: слабое свечение */
                uint8_t v = (uint8_t)(10 + (htri >> 6));
                hsv_to_rgb_u8(hue, 200, v, &r, &g, &b);
            }

            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
#endif


/* -------------------- ЭФФЕКТ #70: TUNNEL -------------------- */
/*
 * Идея:
 *   - “туннель” из колец (радиальные полосы) от центра
 *   - движение за счёт фазового сдвига по времени
 */
void fx_tunnel_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = t_scaled_ms(ctx, t_ms);
    const float tf = (float)t * 0.0025f;

    const float cx = (float)(MATRIX_W - 1) * 0.5f;
    const float cy = (float)(MATRIX_H - 1) * 0.5f;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const float dx = (float)x - cx;
            const float dy = (float)y - cy;
            const float d  = sqrtf(dx*dx + dy*dy);

            const float rings = d * 0.55f - tf;
            const float s = 0.5f + 0.5f * sinf(rings);

            const uint8_t v = (uint8_t)(20 + (uint8_t)(s * 235.0f));
            const uint8_t h = (uint8_t)((uint32_t)(d * 18.0f + (float)(t >> 3)) & 0xFF);

            uint8_t r,g,b;
            hsv_to_rgb_u8(h, 240, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* -------------------- ЭФФЕКТ #71: ORBIT DOTS -------------------- */
/*
 * Идея:
 *   - несколько “комет/точек” вращаются вокруг центра
 *   - рисуем как мягкие “блобики” по расстоянию до каждой точки
 */
void fx_orbit_dots_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = t_scaled_ms(ctx, t_ms);
    const float tf = (float)t * 0.0012f;

    const float cx = (float)(MATRIX_W - 1) * 0.5f;
    const float cy = (float)(MATRIX_H - 1) * 0.5f;

    /* 3 орбиты разного радиуса */
    const float r0 = 4.0f;
    const float r1 = 6.2f;
    const float r2 = 8.6f;

    const float x0 = cx + r0 * cosf(tf * 1.3f);
    const float y0 = cy + r0 * sinf(tf * 1.3f);

    const float x1 = cx + r1 * cosf(tf * 0.9f + 1.7f);
    const float y1 = cy + r1 * sinf(tf * 0.9f + 1.7f);

    const float x2 = cx + r2 * cosf(tf * 0.7f + 2.8f);
    const float y2 = cy + r2 * sinf(tf * 0.7f + 2.8f);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const float fx = (float)x;
            const float fy = (float)y;

            float acc = 0.0f;

            float dx = fx - x0; float dy = fy - y0; acc += 1.2f / (1.0f + dx*dx + dy*dy);
            dx = fx - x1; dy = fy - y1;            acc += 1.0f / (1.0f + dx*dx + dy*dy);
            dx = fx - x2; dy = fy - y2;            acc += 0.9f / (1.0f + dx*dx + dy*dy);

            /* нормируем в 0..255 */
            int32_t v = (int32_t)(acc * 220.0f);
            uint8_t vv = clamp_u8_i32(v);

            uint8_t hue = (uint8_t)((t >> 4) + x * 3 + y * 7);
            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, vv, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* -------------------- ЭФФЕКТ #72: MOIRE -------------------- */
/*
 * Идея:
 *   - интерференция двух “волн” по X и Y (moire)
 *   - выглядит как живые полосы/переливы
 * Назначение:
 *   “Moire”/интерференционный узор на базе сумм синусов по X/Y/диагонали.
 *   Цвет берём из HSV, яркость = функция от интерференции.
 *
 * Важно:
 *   - Использует float + sinf() (нагрузка выше, чем у integer/noise эффектов).
 *   - Скорость регулируется через t_scaled_ms(ctx, t_ms).
 */
void fx_moire_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = t_scaled_ms(ctx, t_ms);
    const float tf = (float)t * 0.0020f;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const float fx = (float)x;
            const float fy = (float)y;

            const float a  = sinf(fx * 0.35f + tf);
            const float bf = sinf(fy * 0.55f - tf * 0.8f);
            const float c  = sinf((fx + fy) * 0.22f + tf * 0.6f);

            const float v = (a + bf + c) * (1.0f / 3.0f); // ~ -1..1
            const float n = 0.5f + 0.5f * v;             // 0..1

            const uint8_t vv = (uint8_t)(20u + (uint8_t)(n * 235.0f));
            const uint8_t hh = (uint8_t)((uint32_t)(tf * 40.0f + fx * 4.0f) & 0xFFu);

            uint8_t r, g, b8;
            hsv_to_rgb_u8(hh, 230u, vv, &r, &g, &b8);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b8);
        }
    }
}



/* -------------------- ЭФФЕКТ #73: PARTICLE GRID -------------------- */
/*
 * Идея:
 *   - “сеточное поле” + редкие искры/частицы
 *   - лёгкая геометрия: яркость выше на линиях сетки, плюс “частицы” PRNG
 */
void fx_particle_grid_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = t_scaled_ms(ctx, t_ms);

    uint32_t seed = (uint32_t)(0xA53C9E3Du ^ (ctx->phase + t * 2654435761u));

    const int grid = 4;
    const uint8_t base_hue = (uint8_t)(t >> 5);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {

            /* сетка */
            const bool on_grid = ((x % grid) == 0) || ((y % grid) == 0);

            uint8_t v = on_grid ? 40 : 8;

            /* частицы: редкие вспышки */
            const uint32_t r = xorshift32(&seed);
            if ((r & 0x1FFu) == 0u) { // ~1/512 шанс
                v = 255;
            } else if ((r & 0x7Fu) == 0u) { // ~1/128
                v = (uint8_t)(120 + (r & 0x3Fu));
            }

            /* лёгкая анимация сетки */
            v = (uint8_t)((uint16_t)v + (tri8((uint8_t)(base_hue + x * 7 + y * 11)) >> 4));

            uint8_t hue = (uint8_t)(base_hue + x * 2 + y * 5);
            uint8_t rr,gg,bb;
            hsv_to_rgb_u8(hue, 255, v, &rr, &gg, &bb);
            matrix_ws2812_set_pixel_xy(x, y, rr, gg, bb);
        }
    }
}
