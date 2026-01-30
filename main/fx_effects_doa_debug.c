#include <stdbool.h>

// ===== DOA debug build switches (single source of truth) =====
// 0 = debug OFF (no selectable DOA DEBUG fx, no DOA logs)
// 1 = debug ON
#ifndef J_DOA_DEBUG_ENABLE
#define J_DOA_DEBUG_ENABLE 0
#endif

#ifndef J_DOA_DEBUG_LOG_ENABLE
#define J_DOA_DEBUG_LOG_ENABLE J_DOA_DEBUG_ENABLE
#endif

bool j_doa_debug_ui_enabled(void)  { return (J_DOA_DEBUG_ENABLE != 0); }
bool j_doa_debug_log_enabled(void) { return (J_DOA_DEBUG_LOG_ENABLE != 0); }


#include <string.h>
#include <math.h>

#include "esp_log.h"

#include "fx_engine.h"
#include "fx_canvas.h"
#include "matrix_ws2812.h"

#include "asr_debug.h"
#include "doa_probe.h"

static const char *TAG = "FX_DOA_DEBUG";

/* ------------------------------ Tuning ------------------------------ */

// Геометрия матрицы уже фиксирована проектом: 16x48
#define DOA_W   16u
#define DOA_H   48u

// Угол -> X
#define DOA_OFFSET_DEG       0.0f     // можно потом вынести в cfg
#define DOA_INVERT           0        // 0=по часовой, 1=инвертировать

// Сила голоса (0..1) -> Y
// Ниже этого порога пиксель не рисуем вообще
#define DOA_LEVEL_MIN        0.05f

// На этом уровне (и выше) пиксель уходит в верхнюю границу диапазона Y
#define DOA_LEVEL_FULL       0.70f

// Диапазон Y, который используем под индикацию (0..DOA_H-1).
// Можно сузить диапазон, если хочешь “не по всему экрану”.
#define DOA_Y_MIN            0u
#define DOA_Y_MAX            (DOA_H - 1u)

// Fade по “старости” DOA данных: держим последний угол, но гасим
#define DOA_FADE_START_MS    150u
#define DOA_FADE_END_MS      900u

/* ------------------------------ Helpers ------------------------------ */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float fade_from_age_ms(uint32_t age_ms)
{
    if (age_ms <= DOA_FADE_START_MS) return 1.0f;
    if (age_ms >= DOA_FADE_END_MS)   return 0.0f;

    const float span = (float)(DOA_FADE_END_MS - DOA_FADE_START_MS);
    const float x    = (float)(age_ms - DOA_FADE_START_MS) / span;
    return 1.0f - clampf(x, 0.0f, 1.0f);
}

static uint8_t map_deg_to_x(float deg)
{
    // deg expected [0..360)
    deg += DOA_OFFSET_DEG;

    // wrap
    while (deg >= 360.0f) deg -= 360.0f;
    while (deg <  0.0f)   deg += 360.0f;

    if (DOA_INVERT) {
        deg = 360.0f - deg;
        if (deg >= 360.0f) deg -= 360.0f;
    }

    // 0..360 -> 0..15
    // Важно: 360 не должен давать 16
    const float x_f = (deg / 360.0f) * (float)DOA_W;
    int x = (int)floorf(x_f);

    if (x < 0) x = 0;
    if (x >= (int)DOA_W) x = (int)DOA_W - 1;
    return (uint8_t)x;
}

static bool map_level_to_y(float level01, uint8_t *out_y, float *out_norm01)
{
    if (level01 < DOA_LEVEL_MIN) return false;

    const float norm = (level01 - DOA_LEVEL_MIN) / (DOA_LEVEL_FULL - DOA_LEVEL_MIN);
    const float n01  = clampf(norm, 0.0f, 1.0f);

    const uint32_t y_span = (uint32_t)(DOA_Y_MAX - DOA_Y_MIN);
    const uint32_t y      = DOA_Y_MAX - (uint32_t)lroundf(n01 * (float)y_span);

    if (out_y)      *out_y      = (uint8_t)y;
    if (out_norm01) *out_norm01 = n01;
    return true;
}

/* ------------------------------ Public render ------------------------------ */

void fx_doa_debug_render(fx_ctx_t *fx, uint32_t t_ms)
{
    (void)fx;
    (void)t_ms;

    // Clear frame (движок canvas не использует, поэтому чистим матрицу напрямую)
    for (uint8_t yy = 0; yy < DOA_H; yy++) {
        for (uint8_t xx = 0; xx < DOA_W; xx++) {
            matrix_ws2812_set_pixel_xy((uint16_t)xx, (uint16_t)yy, 0, 0, 0);
        }
    }

    // DOA angle snapshot
    doa_snapshot_t s;
    const bool have = doa_probe_get_snapshot(&s);
    if (!have) return;

    // Voice level (0..1)
    const float level01 = clampf(asr_debug_get_level(), 0.0f, 1.0f);

    uint8_t y = 0;
    float level_norm01 = 0.0f;
    if (!map_level_to_y(level01, &y, &level_norm01)) {
        return;
    }

    const uint8_t x = map_deg_to_x(s.azimuth_deg);

    const float fade_age = fade_from_age_ms(s.age_ms);
    const float bri      = clampf(0.25f + 0.75f * level_norm01, 0.0f, 1.0f) * fade_age;

    const uint8_t v = (uint8_t)lroundf(255.0f * clampf(bri, 0.0f, 1.0f));

    const uint8_t r = (uint8_t)(v / 12u);
    const uint8_t g = v;
    const uint8_t b = v;

    matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y, r, g, b);

    (void)TAG;
}

