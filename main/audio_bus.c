#include "audio_bus.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "audio_player.h"

static const char *TAG = "AUDIO_BUS";

/* task/queue config */
#define AUDIO_BUS_TASK_CORE          (0)
#define AUDIO_BUS_TASK_PRIO          (9)
#define AUDIO_BUS_TASK_STACK_BYTES   (4096)
#define AUDIO_BUS_QUEUE_LEN          (16)

/* NVS persist */
#define AUDIO_NVS_NS                 "jinny"
#define AUDIO_NVS_KEY_VOL            "audio_vol"
#define AUDIO_VOL_DEFAULT            (70u)

/* debounce persist: если slider шлёт часто, сохраняем после паузы */
#define AUDIO_VOL_PERSIST_DEBOUNCE_MS (800u)

static QueueHandle_t    s_q = NULL;
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t     s_task = NULL;

static audio_state_t    s_st = {
    .volume_pct = AUDIO_VOL_DEFAULT,
    .seq = 0,
};

static bool     s_dirty = false;
static uint32_t s_dirty_deadline_ms = 0;

static uint32_t now_ms(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

static void st_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void st_unlock(void) { xSemaphoreGive(s_lock); }

static void nvs_load_volume(uint8_t *out_vol)
{
    *out_vol = AUDIO_VOL_DEFAULT;

    nvs_handle_t h;
    esp_err_t err = nvs_open(AUDIO_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return;
    }

    uint8_t v = AUDIO_VOL_DEFAULT;
    err = nvs_get_u8(h, AUDIO_NVS_KEY_VOL, &v);
    nvs_close(h);

    if (err == ESP_OK) {
        if (v > 100) v = 100;
        *out_vol = v;
    }
}

static void nvs_save_volume(uint8_t vol)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(AUDIO_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RW failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u8(h, AUDIO_NVS_KEY_VOL, vol);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_save vol=%u failed: %s", (unsigned)vol, esp_err_to_name(err));
    }
}

static void apply_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;

    st_lock();
    if (s_st.volume_pct == vol) {
        st_unlock();
        return;
    }
    s_st.volume_pct = vol;
    s_st.seq++;
    s_dirty = true;
    s_dirty_deadline_ms = now_ms() + AUDIO_VOL_PERSIST_DEBOUNCE_MS;
    st_unlock();

    audio_player_set_volume_pct(vol);
    ESP_LOGI(TAG, "Volume set to %u%% (seq=%lu)", (unsigned)vol, (unsigned long)s_st.seq);
}

static void audio_bus_task(void *arg)
{
    (void)arg;

    audio_cmd_t cmd;
    for (;;) {
        /* ждём команду, но просыпаемся периодически чтобы обработать debounce persist */
        if (xQueueReceive(s_q, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (cmd.type) {
                case AUDIO_CMD_SET_VOLUME:
                    apply_volume(cmd.volume_pct);
                    break;
                case AUDIO_CMD_GET_STATE:
                default:
                    /* ничего: state читается через audio_bus_get_state() */
                    break;
            }
        }

        /* debounce persist */
        st_lock();
        const bool do_save = s_dirty && ((int32_t)(now_ms() - s_dirty_deadline_ms) >= 0);
        const uint8_t vol = s_st.volume_pct;
        if (do_save) {
            s_dirty = false;
        }
        st_unlock();

        if (do_save) {
            nvs_save_volume(vol);
        }
    }
}

esp_err_t audio_bus_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

    if (!s_q) {
        s_q = xQueueCreate(AUDIO_BUS_QUEUE_LEN, sizeof(audio_cmd_t));
        if (!s_q) return ESP_ERR_NO_MEM;
    }

    /* загрузить volume из NVS до старта task */
    uint8_t vol = AUDIO_VOL_DEFAULT;
    nvs_load_volume(&vol);

    st_lock();
    s_st.volume_pct = vol;
    s_st.seq = 1;
    s_dirty = false;
    st_unlock();

    audio_player_set_volume_pct(vol);

    if (!s_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            audio_bus_task,
            "audio_bus",
            AUDIO_BUS_TASK_STACK_BYTES,
            NULL,
            AUDIO_BUS_TASK_PRIO,
            &s_task,
            AUDIO_BUS_TASK_CORE);

        if (ok != pdPASS) {
            s_task = NULL;
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "audio_bus started (vol=%u%%)", (unsigned)vol);
    return ESP_OK;
}

esp_err_t audio_bus_submit(const audio_cmd_t *cmd)
{
    if (!cmd || !s_q) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_q, cmd, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void audio_bus_get_state(audio_state_t *out)
{
    if (!out) return;
    st_lock();
    *out = s_st;
    st_unlock();
}
