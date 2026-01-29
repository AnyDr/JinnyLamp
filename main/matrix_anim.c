#include "matrix_anim.h"

#include "matrix_ws2812.h"
#include "fx_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

/* ============================================================
 * Configuration
 * ============================================================ */

#define MATRIX_ANIM_FPS            15
#define MATRIX_ANIM_FRAME_MS       (1000 / MATRIX_ANIM_FPS)

/* Task notify bits */
#define ANIM_NOTIFY_STOP_REQUEST   (1u << 0)
#define ANIM_NOTIFY_STOPPED_ACK    (1u << 1)

/* ============================================================
 * Internal state
 * ============================================================ */

static const char *TAG = "MATRIX_ANIM";

static TaskHandle_t s_task = NULL;
static TaskHandle_t s_waiter_task = NULL;

/* Pause flag */
static bool s_paused = false;

static uint32_t s_wall_ms = 0;
static uint32_t s_wall_last_ms = 0;
static uint32_t s_anim_ms = 0;
static uint16_t s_last_effect_id = 0;

/* Lock */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

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

        const uint32_t now_wall_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        const uint32_t wall_dt_ms = now_wall_ms - s_wall_last_ms;
        s_wall_last_ms = now_wall_ms;
        s_wall_ms = now_wall_ms;

        bool paused;
        portENTER_CRITICAL(&s_lock);
        paused = s_paused;
        portEXIT_CRITICAL(&s_lock);

        // Если сменили эффект — по ТЗ anim-time обнуляем.
        const uint16_t cur_fx = fx_engine_get_effect();
        if (cur_fx != s_last_effect_id) {
            s_last_effect_id = cur_fx;
            s_anim_ms = 0;
        }

        // speed_pct масштабирует anim-time (master clock)
        const uint16_t spd = fx_engine_get_speed_pct(); // 10..300

        uint32_t anim_dt_ms = 0;
        if (!paused) {
            anim_dt_ms = (uint32_t)(((uint64_t)wall_dt_ms * (uint64_t)spd) / 100ULL);
            if (anim_dt_ms == 0 && wall_dt_ms != 0) {
                anim_dt_ms = 1;
            }
            s_anim_ms += anim_dt_ms;
        }

        fx_engine_render(
            s_wall_ms,
            wall_dt_ms,
            s_anim_ms,
            anim_dt_ms
        );

        const esp_err_t err = matrix_ws2812_show();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "matrix show failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(MATRIX_ANIM_FRAME_MS));
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
    portENTER_CRITICAL(&s_lock);
    s_paused = !s_paused;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "pause %s", s_paused ? "ON" : "OFF");
}

bool matrix_anim_is_paused(void)
{
    bool paused;
    portENTER_CRITICAL(&s_lock);
    paused = s_paused;
    portEXIT_CRITICAL(&s_lock);
    return paused;
}
