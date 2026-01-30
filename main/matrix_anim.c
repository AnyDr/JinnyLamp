#include "matrix_anim.h"

#include "matrix_ws2812.h"
#include "fx_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"   // heap_caps_get_free_size/minimum_free_size

/* ============================================================
 * Configuration
 * ============================================================ */

#define MATRIX_ANIM_FPS            22
#define MATRIX_ANIM_FRAME_MS       (1000 / MATRIX_ANIM_FPS)

/* Task notify bits */
#define ANIM_NOTIFY_STOP_REQUEST   (1u << 0)
#define ANIM_NOTIFY_STOPPED_ACK    (1u << 1)

/*
 * Enable performance logs (ANIM_PERF + ANIM_SYS).
 * Keep this OFF by default to avoid log spam / timing perturbation.
 */
#ifndef J_MATRIX_ANIM_PERF_DEBUG
#define J_MATRIX_ANIM_PERF_DEBUG   0
#endif

/* ============================================================
 * Internal state
 * ============================================================ */

static const char *TAG = "MATRIX_ANIM";

static TaskHandle_t s_task = NULL;
static TaskHandle_t s_waiter_task = NULL;

static uint32_t s_wall_ms = 0;
static uint32_t s_wall_last_ms = 0;
static uint32_t s_anim_ms = 0;
static uint16_t s_last_effect_id = 0;
static bool s_paused = false; // legacy mirror (not a source of truth)

/* ============================================================
 * Animation task
 * ============================================================ */

static void matrix_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG,
             "matrix task started (W=%u H=%u leds=%u fps=%u)",
             MATRIX_PANEL_W,
             MATRIX_PANEL_H * MATRIX_PANELS,
             MATRIX_PANEL_W * MATRIX_PANEL_H * MATRIX_PANELS,
             MATRIX_ANIM_FPS);

    // init wall clock
    s_wall_last_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    s_wall_ms = s_wall_last_ms;

    // init anim clock
    s_anim_ms = 0;
    s_last_effect_id = fx_engine_get_effect();

    TickType_t last_wake = xTaskGetTickCount();

#if J_MATRIX_ANIM_PERF_DEBUG
    // PERF window accumulators (1-second window)
    int64_t  s_prof_last_us = 0;
    uint32_t s_frames = 0;
    uint32_t s_miss = 0;

    int32_t s_r_min =  1000000000, s_r_max = 0, s_r_sum = 0;
    int32_t s_s_min =  1000000000, s_s_max = 0, s_s_sum = 0;
    int32_t s_t_min =  1000000000, s_t_max = 0, s_t_sum = 0;

    int64_t s_sys_last_us = 0;
#endif

    while (1) {
        // stop request (non-blocking poll)
        uint32_t notif = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &notif, 0);

        if (notif & ANIM_NOTIFY_STOP_REQUEST) {
            if (s_waiter_task) {
                xTaskNotify(s_waiter_task, ANIM_NOTIFY_STOPPED_ACK, eSetBits);
            }
            break;
        }

        // wall clock
        const uint32_t now_wall_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        const uint32_t wall_dt_ms = now_wall_ms - s_wall_last_ms;
        s_wall_last_ms = now_wall_ms;
        s_wall_ms = now_wall_ms;

        // pause (source of truth = fx_engine)
        const bool paused = fx_engine_get_paused();
        s_paused = paused; // keep mirror for legacy/debug

        // If effect changed -> reset anim time per spec
        const uint16_t cur_fx = fx_engine_get_effect();
        if (cur_fx != s_last_effect_id) {
            s_last_effect_id = cur_fx;
            s_anim_ms = 0;
        }

        // speed_pct scales anim-time (master clock)
        const uint16_t spd = fx_engine_get_speed_pct(); // 10..300

        uint32_t anim_dt_ms = 0;
        if (!paused) {
            anim_dt_ms = (uint32_t)(((uint64_t)wall_dt_ms * (uint64_t)spd) / 100ULL);
            if (anim_dt_ms == 0 && wall_dt_ms != 0) {
                anim_dt_ms = 1;
            }
            s_anim_ms += anim_dt_ms;
        }

#if J_MATRIX_ANIM_PERF_DEBUG
        const int64_t t_frame_start_us = esp_timer_get_time();
#endif

        // render + show (single path per frame)
        fx_engine_render(s_wall_ms, wall_dt_ms, s_anim_ms, anim_dt_ms);

#if J_MATRIX_ANIM_PERF_DEBUG
        const int64_t t_after_render_us = esp_timer_get_time();
#endif

        const esp_err_t err = matrix_ws2812_show();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "matrix show failed: %s", esp_err_to_name(err));
        }

#if J_MATRIX_ANIM_PERF_DEBUG
        const int64_t t_after_show_us = esp_timer_get_time();

        // ---- PERF stats (1 second window) ----
        const int32_t render_us = (int32_t)(t_after_render_us - t_frame_start_us);
        const int32_t show_us   = (int32_t)(t_after_show_us - t_after_render_us);
        const int32_t total_us  = render_us + show_us;
        const int32_t budget_us = (int32_t)MATRIX_ANIM_FRAME_MS * 1000;

        s_frames++;

        if (render_us < s_r_min) s_r_min = render_us;
        if (render_us > s_r_max) s_r_max = render_us;
        s_r_sum += render_us;

        if (show_us < s_s_min) s_s_min = show_us;
        if (show_us > s_s_max) s_s_max = show_us;
        s_s_sum += show_us;

        if (total_us < s_t_min) s_t_min = total_us;
        if (total_us > s_t_max) s_t_max = total_us;
        s_t_sum += total_us;

        if (total_us > budget_us) s_miss++;

        if (t_after_show_us - s_prof_last_us >= 1000000) {
            s_prof_last_us = t_after_show_us;

            const int32_t r_avg = (s_frames ? (s_r_sum / (int32_t)s_frames) : 0);
            const int32_t s_avg = (s_frames ? (s_s_sum / (int32_t)s_frames) : 0);
            const int32_t t_avg = (s_frames ? (s_t_sum / (int32_t)s_frames) : 0);

            ESP_LOGI("ANIM_PERF",
                     "fps=%u budget=%dus frames=%u miss=%u | render us min/avg/max=%d/%d/%d | show us min/avg/max=%d/%d/%d | total us min/avg/max=%d/%d/%d",
                     (unsigned)MATRIX_ANIM_FPS,
                     (int)budget_us, (unsigned)s_frames, (unsigned)s_miss,
                     (int)s_r_min, (int)r_avg, (int)s_r_max,
                     (int)s_s_min, (int)s_avg, (int)s_s_max,
                     (int)s_t_min, (int)t_avg, (int)s_t_max);

            // reset window
            s_frames = 0;
            s_miss = 0;
            s_r_min = s_s_min = s_t_min = 1000000000;
            s_r_max = s_s_max = s_t_max = 0;
            s_r_sum = s_s_sum = s_t_sum = 0;
        }

        // ---- SYS stats (5 seconds) ----
        if (t_after_show_us - s_sys_last_us >= 5000000) {
            s_sys_last_us = t_after_show_us;

            const size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            const size_t heap_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
            const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(NULL);

            ESP_LOGI("ANIM_SYS",
                     "heap free=%u min=%u bytes | anim stack HWM=%u words",
                     (unsigned)heap_free, (unsigned)heap_min, (unsigned)hwm_words);
        }
#endif

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MATRIX_ANIM_FRAME_MS));
    }

    // task is exiting
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ============================================================
 * Public API
 * ============================================================ */

void matrix_anim_start(void)
{
    if (s_task != NULL) {
        ESP_LOGW(TAG, "matrix task already running");
        return;
    }

    s_paused = false;
    s_wall_ms = 0;
    s_wall_last_ms = 0;
    s_anim_ms = 0;
    s_last_effect_id = 0;

    xTaskCreatePinnedToCore(
        matrix_task,
        "matrix_anim",
        4096,
        NULL,
        5,
        &s_task,
        1);

    ESP_LOGI(TAG, "matrix animation started");
}

void matrix_anim_stop(void)
{
    if (s_task == NULL) {
        return;
    }

    // fire-and-forget: ask task to exit
    xTaskNotify(s_task, ANIM_NOTIFY_STOP_REQUEST, eSetBits);
    ESP_LOGI(TAG, "matrix animation stop requested");
}

void matrix_anim_stop_and_wait(void)
{
    TaskHandle_t t = s_task;
    if (t == NULL) {
        return;
    }

    s_waiter_task = xTaskGetCurrentTaskHandle();

    xTaskNotify(t, ANIM_NOTIFY_STOP_REQUEST, eSetBits);

    uint32_t notif = 0;
    const TickType_t to = pdMS_TO_TICKS(1000);
    const BaseType_t ok = xTaskNotifyWait(0, UINT32_MAX, &notif, to);

    s_waiter_task = NULL;

    if (ok == pdTRUE && (notif & ANIM_NOTIFY_STOPPED_ACK)) {
        ESP_LOGI(TAG, "matrix animation stopped (join ok)");
        return;
    }

    ESP_LOGW(TAG, "stop_and_wait timeout -> force delete");
    vTaskDelete(t);
    s_task = NULL;
}

void matrix_anim_pause_toggle(void)
{
    const bool new_paused = !fx_engine_get_paused();
    fx_engine_pause_set(new_paused);
    ESP_LOGI(TAG, "pause %s", new_paused ? "ON" : "OFF");
}

bool matrix_anim_is_paused(void)
{
    return fx_engine_get_paused();
}
