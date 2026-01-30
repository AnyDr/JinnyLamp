// main/fx_effects_simple.c
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
 * Набор простых эффектов (7 шт) под "New Time Approach":
 * - time source: ctx->anim_ms / ctx->anim_dt_ms (уже speed-scaled в matrix_anim)
 * - НЕ использовать ctx->speed_pct для тайминга (чтобы не было double-speed)
 * ============================================================ */

typedef struct { uint8_t r,g,b; } rgb8_t;

/* ---------------- helpers ---------------- */

static inline uint32_t xorshift32_u32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline uint32_t xorshift32(uint32_t *state) { return xorshift32_u32(state); }

static inline void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint8_t region = h / 43;
    const uint8_t rem = (h - (region * 43)) * 6;

    const uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
    const uint8_t q = (uint8_t)((v * (255 - ((s * rem) >> 8))) >> 8);
    const uint8_t t = (uint8_t)((v * (255 - ((s * (255 - rem)) >> 8))) >> 8);

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

/* small local framebuffer for confetti */
static rgb8_t s_conf[MATRIX_LEDS_TOTAL];
static uint8_t s_conf_init = 0;

static inline void conf_fade(uint8_t keep)
{
    for (uint16_t i = 0; i < MATRIX_LEDS_TOTAL; i++) {
        s_conf[i].r = (uint8_t)((uint16_t)s_conf[i].r * keep / 255u);
        s_conf[i].g = (uint8_t)((uint16_t)s_conf[i].g * keep / 255u);
        s_conf[i].b = (uint8_t)((uint16_t)s_conf[i].b * keep / 255u);
    }
}

static inline void conf_present(void)
{
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t i = (uint16_t)(y * MATRIX_W + x);
            matrix_ws2812_set_pixel_xy(x, y, s_conf[i].r, s_conf[i].g, s_conf[i].b);
        }
    }
}

static inline void fill_black(void)
{
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
        }
    }
}

/* ---------------- FX: SNOW FALL ---------------- */

void fx_snow_fall_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    uint8_t dim = 243;
    const uint32_t dt = ctx->anim_dt_ms;
    if (dt >= 90u)  dim = 247;
    if (dt >= 140u) dim = 250;

    fx_canvas_dim(dim);
    fx_canvas_shift_towards_y0(0, 0, 0);

    uint8_t flakes = 2;
    if (ctx->anim_dt_ms >= 80u)  flakes = 3;
    if (ctx->anim_dt_ms >= 120u) flakes = 4;

    for (uint8_t i = 0; i < flakes; i++) {
        const uint32_t r = esp_random();
        const uint16_t x = (uint16_t)(r % MATRIX_W);
        const uint8_t  v = (uint8_t)(200 + (r & 0x37));
        fx_canvas_set(x, (uint16_t)(MATRIX_H - 1), v, v, v);
    }

    fx_canvas_present();
}

/* ---------------- FX: CONFETTI ---------------- */

void fx_confetti_render(fx_ctx_t *ctx)
{
    if (!s_conf_init) {
        for (uint16_t i = 0; i < MATRIX_LEDS_TOTAL; i++) s_conf[i] = (rgb8_t){0,0,0};
        s_conf_init = 1;
    }

    if (!ctx) return;

    const uint32_t dt = ctx->anim_dt_ms; // already speed-scaled by master clock

    const uint8_t fade = (dt >= 120u) ? 210u : ((dt >= 80u) ? 220u : 232u);
    conf_fade(fade);

    const uint32_t phase = (ctx->anim_ms / 20u);
    uint32_t s = (uint32_t)(0xC0FF377u ^ (phase * 33u) ^ (ctx->wall_ms * 17u));

    uint8_t pops = 1u + (uint8_t)(dt / 35u);
    if (pops > 6u) pops = 6u;

    for (uint8_t k = 0; k < pops; k++) {
        const uint32_t r0 = xorshift32_u32(&s);
        const uint16_t x = (uint16_t)(r0 % MATRIX_W);
        const uint16_t y = (uint16_t)((r0 >> 8) % MATRIX_H);

        uint8_t rr, gg, bb;
        const uint8_t hue = (uint8_t)(r0 >> 16);
        hsv_to_rgb(hue, 255, 220, &rr, &gg, &bb);

        const uint16_t i = (uint16_t)(y * MATRIX_W + x);
        s_conf[i] = (rgb8_t){ rr, gg, bb };
    }

    conf_present();
}

/* ---------------- FX: DIAG RAINBOW ---------------- */

void fx_diag_rainbow_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    const uint32_t phase = (ctx->anim_ms / 20u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + (x * 7u) + (y * 9u));
            uint8_t r,g,b;
            hsv_to_rgb(h, 255, 210, &r,&g,&b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ---------------- FX: GLITTER RAINBOW ---------------- */

void fx_glitter_rainbow_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    const uint32_t phase = (ctx->anim_ms / 20u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t h = (uint8_t)(phase + x * 6u);
            uint8_t r,g,b;
            hsv_to_rgb(h, 255, 180, &r,&g,&b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }

    // small glitter, deterministic from time
    uint32_t s = (uint32_t)(0x9E3779B9u ^ (phase * 33u) ^ (ctx->wall_ms * 17u));
    for (int i = 0; i < 16; i++) {
        const uint32_t rr = xorshift32(&s);
        const uint16_t x = (uint16_t)(rr % MATRIX_W);
        const uint16_t y = (uint16_t)((rr >> 8) % MATRIX_H);
        matrix_ws2812_set_pixel_xy(x, y, 255, 255, 255);
    }
}

/* ---------------- FX: RADIAL RIPPLE ---------------- */

void fx_radial_ripple_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    const uint32_t phase = (ctx->anim_ms / 18u);

    const int16_t cx = (int16_t)(MATRIX_W / 2u);
    const int16_t cy = (int16_t)(MATRIX_H / 2u);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const int16_t dx = (int16_t)x - cx;
            const int16_t dy = (int16_t)y - cy;
            const uint16_t dist = (uint16_t)(abs(dx) + abs(dy)); // cheap "radius"
            const uint8_t h = (uint8_t)(phase + dist * 9u);

            uint8_t v = 200;
            const uint8_t m = (uint8_t)((phase + dist * 12u) & 0xFFu);
            if (m < 40u) v = 255;

            uint8_t r,g,b;
            hsv_to_rgb(h, 255, v, &r,&g,&b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

/* ---------------- FX: CUBES ---------------- */

void fx_cubes_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    fill_black();

    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;
    const int cx = w / 2;
    const int cy = h / 2;

    const float t = (float)ctx->anim_ms * 0.0020f;

    for (int k = 0; k < 4; k++) {
        const float pulse = 0.55f + 0.45f * sinf(t + (float)k * 0.9f);
        int hw = (int)((10 + k * 6) * pulse);
        int hh = (int)((5  + k * 3) * pulse);

        if (hw < 2) hw = 2;
        if (hh < 2) hh = 2;

        const uint8_t hue = (uint8_t)((ctx->anim_ms >> 4) + (uint8_t)(k * 50));
        uint8_t r,g,b;
        hsv_to_rgb(hue, 255, 200, &r,&g,&b);

        for (int x = cx - hw; x <= cx + hw; x++) {
            const int y1 = cy - hh;
            const int y2 = cy + hh;
            if ((unsigned)x < MATRIX_W) {
                if ((unsigned)y1 < MATRIX_H) matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y1, r,g,b);
                if ((unsigned)y2 < MATRIX_H) matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y2, r,g,b);
            }
        }

        for (int y = cy - hh; y <= cy + hh; y++) {
            const int x1 = cx - hw;
            const int x2 = cx + hw;
            if ((unsigned)y < MATRIX_H) {
                if ((unsigned)x1 < MATRIX_W) matrix_ws2812_set_pixel_xy((uint16_t)x1, (uint16_t)y, r,g,b);
                if ((unsigned)x2 < MATRIX_W) matrix_ws2812_set_pixel_xy((uint16_t)x2, (uint16_t)y, r,g,b);
            }
        }
    }

    uint32_t seed = 0xA53C91u ^ (ctx->anim_ms * 2654435761u);
    for (int i = 0; i < 12; i++) {
        const uint32_t rr = xorshift32(&seed);
        const uint16_t x = (uint16_t)(rr % MATRIX_W);
        const uint16_t y = (uint16_t)((rr >> 8) % MATRIX_H);
        matrix_ws2812_set_pixel_xy(x, y, 255, 255, 255);
    }
}

/* ---------------- FX: ORBIT DOTS ---------------- */

void fx_orbit_dots_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    const uint32_t t = ctx->anim_ms;
    const float tf = (float)t * 0.0012f;

    const float cx = (float)(MATRIX_W - 1) * 0.5f;
    const float cy = (float)(MATRIX_H - 1) * 0.5f;

    const float r0 = 4.0f;
    const float r1 = 6.0f;
    const float r2 = 8.0f;

    const float a0 = tf;
    const float a1 = -tf * 1.3f;
    const float a2 = tf * 0.7f;

    const int x0 = (int)(cx + cosf(a0) * r0);
    const int y0 = (int)(cy + sinf(a0) * r0);

    const int x1 = (int)(cx + cosf(a1) * r1);
    const int y1 = (int)(cy + sinf(a1) * r1);

    const int x2 = (int)(cx + cosf(a2) * r2);
    const int y2 = (int)(cy + sinf(a2) * r2);

    fill_black();

    if ((unsigned)x0 < MATRIX_W && (unsigned)y0 < MATRIX_H) matrix_ws2812_set_pixel_xy((uint16_t)x0, (uint16_t)y0, 255, 80, 40);
    if ((unsigned)x1 < MATRIX_W && (unsigned)y1 < MATRIX_H) matrix_ws2812_set_pixel_xy((uint16_t)x1, (uint16_t)y1, 40, 255, 80);
    if ((unsigned)x2 < MATRIX_W && (unsigned)y2 < MATRIX_H) matrix_ws2812_set_pixel_xy((uint16_t)x2, (uint16_t)y2, 80, 40, 255);

    // small trail
    for (int i = 0; i < 8; i++) {
        const float aa = tf - (float)i * 0.25f;
        const int xt = (int)(cx + cosf(aa) * r1);
        const int yt = (int)(cy + sinf(aa) * r1);
        if ((unsigned)xt < MATRIX_W && (unsigned)yt < MATRIX_H) {
            matrix_ws2812_set_pixel_xy((uint16_t)xt, (uint16_t)yt, 40, 40, 40);
        }
    }
}
