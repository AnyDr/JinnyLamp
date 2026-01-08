// main/fx_effects_minset.c
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "fx_engine.h"
#include "fx_canvas.h"
#include "matrix_ws2812.h"
#include "esp_random.h"

/* ============================================================
 * fx_effects_simple.c
 *
 * Минимальный набор эффектов (7 шт), собранный в один TU:
 *   - SNOW FALL
 *   - CONFETTI
 *   - DIAG RAINBOW
 *   - GLITTER RAINBOW
 *   - RADIAL RIPPLE
 *   - CUBES
 *   - ORBIT DOTS
 *
 * Важно:
 *   - Без динамических аллокаций.
 *   - Каждый render полностью рисует кадр.

 * ============================================================ */

typedef struct { uint8_t r, g, b; } rgb8_t;

/* ---------------- общие хелперы ---------------- */

static inline uint32_t speed_mul(const fx_ctx_t *ctx)
{
    uint32_t sp = (uint32_t)ctx->speed_pct;
    if (sp < 10)  sp = 10;
    if (sp > 300) sp = 300;
    return sp;
}

/* tri wave 0..255..0 (phase 0..511) */
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

/* HSV(0..255) -> RGB(0..255) без float (универсально для “простых” эффектов) */
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

/* ---------------- SNOW FALL (из fx_effects_ported_1.c) ---------------- */

void fx_snow_fall_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    uint8_t dim = 210;
    if (ctx->speed_pct >= 200) dim = 225;
    if (ctx->speed_pct >= 260) dim = 235;

    fx_canvas_dim(dim);
    fx_canvas_shift_down(0, 0, 0);

    uint8_t flakes = 2;
    if (ctx->speed_pct >= 140) flakes = 3;
    if (ctx->speed_pct >= 220) flakes = 4;

    for (uint8_t i = 0; i < flakes; i++) {
        const uint32_t r = esp_random();
        const uint16_t x = (uint16_t)(r % MATRIX_W);
        const uint8_t  v = (uint8_t)(200 + (r & 0x37));
        fx_canvas_set(x, 0, v, v, v);
    }

    fx_canvas_present();
}

/* ---------------- CONFETTI (из fx_effects_noise_2.c) ---------------- */

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t t)
{
    return (uint8_t)(a + (uint16_t)(b - a) * (uint16_t)t / 255u);
}

static inline rgb8_t lerp_rgb(rgb8_t a, rgb8_t b, uint8_t t)
{
    rgb8_t o = { lerp_u8(a.r, b.r, t), lerp_u8(a.g, b.g, t), lerp_u8(a.b, b.b, t) };
    return o;
}

static rgb8_t palette16_sample(const rgb8_t pal[16], uint8_t idx)
{
    const uint8_t seg  = (uint8_t)(idx >> 4);
    const uint8_t frac = (uint8_t)((idx & 0x0Fu) * 17u);
    const rgb8_t a = pal[seg];
    const rgb8_t b = pal[(uint8_t)((seg + 1u) & 0x0Fu)];
    return lerp_rgb(a, b, frac);
}

static const rgb8_t PAL_RAINBOW[16] = {
    {255,   0,   0}, {255,  64,   0}, {255, 128,   0}, {255, 192,   0},
    {255, 255,   0}, {128, 255,   0}, {  0, 255,   0}, {  0, 255, 128},
    {  0, 255, 255}, {  0, 128, 255}, {  0,   0, 255}, {128,   0, 255},
    {255,   0, 255}, {255,   0, 128}, {255,   0,  64}, {255,   0,   0},
};

static rgb8_t s_conf[MATRIX_LEDS_TOTAL];
static uint8_t s_conf_init = 0;

static inline uint32_t xorshift32_u32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline void conf_fade(uint8_t fade)
{
    for (uint16_t i = 0; i < MATRIX_LEDS_TOTAL; i++) {
        s_conf[i].r = (uint8_t)((uint16_t)s_conf[i].r * (uint16_t)fade / 255u);
        s_conf[i].g = (uint8_t)((uint16_t)s_conf[i].g * (uint16_t)fade / 255u);
        s_conf[i].b = (uint8_t)((uint16_t)s_conf[i].b * (uint16_t)fade / 255u);
    }
}

void fx_confetti_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    if (!s_conf_init) {
        for (uint16_t i = 0; i < MATRIX_LEDS_TOTAL; i++) s_conf[i] = (rgb8_t){0,0,0};
        s_conf_init = 1;
    }

    const uint32_t spd = (ctx->speed_pct < 10u) ? 10u : ctx->speed_pct;

    const uint8_t fade = (spd >= 200u) ? 210u : (uint8_t)(230u - (spd / 4u));
    conf_fade(fade);

    uint32_t s = (uint32_t)(0xC0FF377u ^ (ctx->phase + t_ms * 17u));

    uint8_t pops = 1u + (uint8_t)(spd / 60u);
    if (pops > 6u) pops = 6u;

    for (uint8_t k = 0; k < pops; k++) {
        const uint32_t r0 = xorshift32_u32(&s);

        const uint16_t idx = (uint16_t)(r0 % (uint32_t)MATRIX_LEDS_TOTAL);
        const uint8_t  hue = (uint8_t)(r0 >> 16);

        const rgb8_t c = palette16_sample(PAL_RAINBOW, hue);

        s_conf[idx].r = (uint8_t)((uint16_t)s_conf[idx].r + (uint16_t)c.r > 255u ? 255u : (s_conf[idx].r + c.r));
        s_conf[idx].g = (uint8_t)((uint16_t)s_conf[idx].g + (uint16_t)c.g > 255u ? 255u : (s_conf[idx].g + c.g));
        s_conf[idx].b = (uint8_t)((uint16_t)s_conf[idx].b + (uint16_t)c.b > 255u ? 255u : (s_conf[idx].b + c.b));
    }

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t idx = matrix_ws2812_xy_to_index(x, y);
            const rgb8_t c = s_conf[idx];
            matrix_ws2812_set_pixel_xy(x, y, c.r, c.g, c.b);
        }
    }
}

/* ---------------- DIAG RAINBOW + GLITTER (из fx_effects_geo_5.c) ---------------- */

static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void fx_diag_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + (x * 7u) + (y * 11u));
            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, 255, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

void fx_glitter_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    const uint32_t sp = speed_mul(ctx);
    const uint32_t phase = ctx->phase / (3u * 100u / sp + 1u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + x * 6u);
            uint8_t r, g, b;
            hsv_to_rgb_u8(h, 255, 255, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }

    uint32_t s = (uint32_t)(0x9E3779B9u ^ (phase * 33u));
    const uint16_t sparks = (uint16_t)((MATRIX_LEDS_TOTAL / 64u) + 2u);

    for (uint16_t i = 0; i < sparks; i++) {
        uint32_t r0 = xs32(&s);
        const uint16_t x = (uint16_t)(r0 % MATRIX_W);
        const uint16_t y = (uint16_t)((r0 / MATRIX_W) % MATRIX_H);

        if ((r0 & 0x3u) != 0) continue;
        matrix_ws2812_set_pixel_xy(x, y, 255, 255, 255);
    }
}

/* ---------------- RADIAL RIPPLE (из fx_effects_geo_7.c) ---------------- */

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

            const uint8_t v = tri_u8((uint16_t)(phase + dist * 22u));

            uint8_t r, g, b;
            hsv_to_rgb_u8((uint8_t)(phase + dist * 4u), 255, v, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ---------------- CUBES (из fx_effects_geo_12.c) ---------------- */

static inline uint8_t clamp_u8_i32(int32_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void fill_black(void)
{
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
        }
    }
}

/* отдельный HSV для CUBES (как было в geo_12) */
static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = v; *g = v; *b = v; return; }

    const uint8_t region = (uint8_t)(h / 43);
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

static inline uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void fx_cubes_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;
    fill_black();

    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;
    const int cx = w / 2;
    const int cy = h / 2;

    const float t = (float)(ctx->phase & 0xFFFFu) * 0.0020f;

    for (int k = 0; k < 4; k++) {
        const float pulse = 0.55f + 0.45f * sinf(t + (float)k * 0.9f);
        int hw = (int)((10 + k * 6) * pulse);
        int hh = (int)((5  + k * 3) * pulse);

        if (hw < 2) hw = 2;
        if (hh < 2) hh = 2;

        const uint8_t hue = (uint8_t)((ctx->phase >> 8) + (uint8_t)(k * 50));
        uint8_t r,g,b;
        hsv_to_rgb(hue, 255, 200, &r,&g,&b);

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

    uint32_t seed = 0xA53C91u ^ (ctx->phase * 2654435761u);
    for (int i = 0; i < 12; i++) {
        const uint32_t rr = xorshift32(&seed);
        const uint16_t x = (uint16_t)(rr % MATRIX_W);
        const uint16_t y = (uint16_t)((rr >> 8) % MATRIX_H);
        matrix_ws2812_set_pixel_xy(x, y, 40, 40, 40);
    }
}

/* ---------------- ORBIT DOTS (из fx_effects_geo_14.c) ---------------- */

static inline uint32_t t_scaled_ms(const fx_ctx_t *ctx, uint32_t t_ms)
{
    return (uint32_t)(((uint64_t)t_ms * (uint64_t)ctx->speed_pct) / 100ULL);
}

void fx_orbit_dots_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = t_scaled_ms(ctx, t_ms);
    const float tf = (float)t * 0.0012f;

    const float cx = (float)(MATRIX_W - 1) * 0.5f;
    const float cy = (float)(MATRIX_H - 1) * 0.5f;

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

            int32_t vv_i = (int32_t)(acc * 220.0f);
            uint8_t vv = clamp_u8_i32(vv_i);

            uint8_t hue = (uint8_t)((t >> 4) + x * 3 + y * 7);
            uint8_t r,g,b;
            hsv_to_rgb_u8(hue, 255, vv, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}
