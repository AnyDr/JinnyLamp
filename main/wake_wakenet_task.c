#include "wake_wakenet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "audio_stream.h"
#include "voice_fsm.h"

static const char *TAG = "WAKE_TASK";

#define WAKE_TASK_CORE          (0)
#define WAKE_TASK_PRIO          (9)
#define WAKE_TASK_STACK_BYTES   (4096)

/* Читаем небольшими кусками. Если WakeNet потребует другой frame size — поправим по месту. */
#define WAKE_FRAME_SAMPLES      (512)

/* Debounce после детекта, чтобы не ловить “дробовик” */
#define WAKE_DEBOUNCE_MS        (1200)

static TaskHandle_t s_task = NULL;
static int64_t s_next_allowed_wake_ms = 0;

static void wake_task(void *arg)
{
    (void)arg;
    int16_t buf[WAKE_FRAME_SAMPLES];

    ESP_LOGI(TAG, "wake task started (frame=%d samples)", WAKE_FRAME_SAMPLES);

    for (;;) {
        size_t got = 0;
        esp_err_t err = audio_stream_read_mono_s16(buf, WAKE_FRAME_SAMPLES, &got, pdMS_TO_TICKS(200));
        if (err == ESP_OK && got != WAKE_FRAME_SAMPLES) {
            /* Для WakeNet лучше подавать полный фрейм. Недобор просто пропускаем. */
            continue;
        }

        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "audio_stream_read err=%s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* debounced detect */
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms < s_next_allowed_wake_ms) {
            continue;
        }

        if (wake_wakenet_detect(buf, WAKE_FRAME_SAMPLES)) {
            s_next_allowed_wake_ms = now_ms + WAKE_DEBOUNCE_MS;
            ESP_LOGI(TAG, "WAKE DETECTED (debounce=%d ms)", WAKE_DEBOUNCE_MS);
            voice_fsm_on_wake();
        }

    }
}

esp_err_t wake_wakenet_task_start(void)
{
    if (s_task) return ESP_OK;

    BaseType_t ok = xTaskCreatePinnedToCore(
        wake_task,
        "wake_wakenet",
        WAKE_TASK_STACK_BYTES,
        NULL,
        WAKE_TASK_PRIO,
        &s_task,
        WAKE_TASK_CORE);

    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void wake_wakenet_task_stop(void)
{
    if (!s_task) return;
    TaskHandle_t t = s_task;
    s_task = NULL;
    vTaskDelete(t);
}
