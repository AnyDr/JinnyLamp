#include "asr_multinet.h"   // ДОЛЖНО быть первым, чтобы типы были видны

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"

#include "audio_stream.h"

// ESP-SR (managed_components espressif__esp-sr)
#include "model_path.h"              // esp_srmodel_init/filter/deinit + srmodel_list_t
#include "esp_mn_iface.h"            // esp_mn_iface_t, esp_mn_state_t, esp_mn_results_t
#include "esp_mn_models.h"           // esp_mn_handle_from_name()
#include "esp_mn_speech_commands.h"  // esp_mn_commands_*

static const char *TAG = "ASR_MN";

/* ---------- tuning knobs ---------- */
#ifndef ASR_MN_TASK_STACK
#define ASR_MN_TASK_STACK 4096
#endif

#ifndef ASR_MN_TASK_PRIO
#define ASR_MN_TASK_PRIO  6
#endif

#ifndef ASR_MN_READ_TIMEOUT_MS
#define ASR_MN_READ_TIMEOUT_MS 50
#endif

/* ---------- internal state ---------- */
typedef struct {
    TaskHandle_t          task;
    EventGroupHandle_t    eg;

    srmodel_list_t       *models;
    const esp_mn_iface_t *mn;
    model_iface_data_t   *mn_handle;

    int                  samp_chunksize;   // required frame length (samples)
    bool                 active;
    uint32_t             deadline_ms;

    asr_multinet_result_cb_t cb;
    void                *cb_user;
} asr_mn_ctx_t;

static asr_mn_ctx_t s_ctx;

#define EG_BIT_RUN   (1u << 0)

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ---------- phrase IDs (stable local contract) ---------- */
enum {
    PHRASE_CANCEL_SESSION = 1,
    PHRASE_SLEEP          = 2,
    PHRASE_OTA_ENTER      = 3,
    PHRASE_ASK_SERVER     = 4,

    PHRASE_NEXT_EFFECT    = 10,
    PHRASE_PREV_EFFECT    = 11,
    PHRASE_PAUSE_TOGGLE   = 12,

    PHRASE_BRIGHT_UP      = 20,
    PHRASE_BRIGHT_DOWN    = 21,
    PHRASE_SPEED_UP       = 22,
    PHRASE_SPEED_DOWN     = 23,
    PHRASE_VOL_UP         = 24,
    PHRASE_VOL_DOWN       = 25,
    PHRASE_MUTE           = 26,
};

static asr_cmd_t map_phrase_to_cmd(int phrase_id)
{
    switch (phrase_id) {
        case PHRASE_CANCEL_SESSION: return ASR_CMD_CANCEL_SESSION;
        case PHRASE_SLEEP:          return ASR_CMD_SLEEP;
        case PHRASE_OTA_ENTER:      return ASR_CMD_OTA_ENTER;
        case PHRASE_ASK_SERVER:     return ASR_CMD_ASK_SERVER;

        case PHRASE_NEXT_EFFECT:    return ASR_CMD_NEXT_EFFECT;
        case PHRASE_PREV_EFFECT:    return ASR_CMD_PREV_EFFECT;
        case PHRASE_PAUSE_TOGGLE:   return ASR_CMD_PAUSE_TOGGLE;

        case PHRASE_BRIGHT_UP:      return ASR_CMD_BRIGHTNESS_UP;
        case PHRASE_BRIGHT_DOWN:    return ASR_CMD_BRIGHTNESS_DOWN;
        case PHRASE_SPEED_UP:       return ASR_CMD_SPEED_UP;
        case PHRASE_SPEED_DOWN:     return ASR_CMD_SPEED_DOWN;
        case PHRASE_VOL_UP:         return ASR_CMD_VOLUME_UP;
        case PHRASE_VOL_DOWN:       return ASR_CMD_VOLUME_DOWN;
        case PHRASE_MUTE:           return ASR_CMD_MUTE;

        default:                    return ASR_CMD_NONE;
    }
}

static void emit_result(asr_cmd_t cmd, int phrase_id, float prob, const char *label_opt)
{
    if (!s_ctx.cb) return;

    asr_cmd_result_t r;
    memset(&r, 0, sizeof(r));
    r.cmd = cmd;
    r.phrase_id = phrase_id;
    r.prob = prob;

    if (label_opt) {
        strncpy(r.label, label_opt, sizeof(r.label) - 1);
        r.label[sizeof(r.label) - 1] = 0;
    }

    s_ctx.cb(&r, s_ctx.cb_user);
}

static esp_err_t mn_register_default_phrases(void)
{
    esp_err_t err;

    err = esp_mn_commands_clear();
    if (err != ESP_OK) return err;

    // NOTE: phrases должны быть без цифр/спецсимволов (ограничение MultiNet).
    // Тут “v1 минимальный набор”. Позже заменим на твой полный словарь.

    esp_mn_commands_add(PHRASE_CANCEL_SESSION, "cancel session");
    esp_mn_commands_add(PHRASE_CANCEL_SESSION, "cancel");

    esp_mn_commands_add(PHRASE_SLEEP, "sleep");
    esp_mn_commands_add(PHRASE_SLEEP, "go to sleep");

    esp_mn_commands_add(PHRASE_OTA_ENTER, "ota");
    esp_mn_commands_add(PHRASE_OTA_ENTER, "update");
    esp_mn_commands_add(PHRASE_OTA_ENTER, "firmware update");

    esp_mn_commands_add(PHRASE_ASK_SERVER, "ask server");
    esp_mn_commands_add(PHRASE_ASK_SERVER, "server");

    esp_mn_commands_add(PHRASE_NEXT_EFFECT, "forward");
    esp_mn_commands_add(PHRASE_NEXT_EFFECT, "go forward");
    esp_mn_commands_add(PHRASE_NEXT_EFFECT, "change");


    esp_mn_commands_add(PHRASE_PREV_EFFECT, "previous");
    esp_mn_commands_add(PHRASE_PREV_EFFECT, "go back");

    esp_mn_commands_add(PHRASE_PAUSE_TOGGLE, "pause");
    esp_mn_commands_add(PHRASE_PAUSE_TOGGLE, "resume");
    esp_mn_commands_add(PHRASE_PAUSE_TOGGLE, "continue");

    esp_mn_commands_add(PHRASE_BRIGHT_UP, "brighter");
    esp_mn_commands_add(PHRASE_BRIGHT_DOWN, "dimmer");

    esp_mn_commands_add(PHRASE_SPEED_UP, "faster");
    esp_mn_commands_add(PHRASE_SPEED_DOWN, "slower");

    esp_mn_commands_add(PHRASE_VOL_UP, "volume up");
    esp_mn_commands_add(PHRASE_VOL_DOWN, "volume down");
    esp_mn_commands_add(PHRASE_MUTE, "mute");

    // Apply
    esp_mn_error_t *errs = esp_mn_commands_update();
    if (errs) {
        ESP_LOGE(TAG, "esp_mn_commands_update(): phrase errors returned");
        return ESP_FAIL;
    }

    esp_mn_active_commands_print();
    return ESP_OK;
}

static void mn_cleanup(void)
{
    if (s_ctx.mn && s_ctx.mn_handle) {
        s_ctx.mn->destroy(s_ctx.mn_handle);
        s_ctx.mn_handle = NULL;
    }
    s_ctx.mn = NULL;

    if (s_ctx.models) {
        esp_srmodel_deinit(s_ctx.models);
        s_ctx.models = NULL;
    }

    s_ctx.samp_chunksize = 0;
}

static esp_err_t mn_load_and_create(void)
{
    // Partition label MUST be "model"
    s_ctx.models = esp_srmodel_init("model");
    if (!s_ctx.models) {
        ESP_LOGE(TAG, "esp_srmodel_init('model') failed");
        return ESP_FAIL;
    }

    // Filter MultiNet model for English
    char *mn_name = esp_srmodel_filter(s_ctx.models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        ESP_LOGE(TAG, "No MultiNet model found (filter prefix=%s lang=%d)", ESP_MN_PREFIX, ESP_MN_ENGLISH);
        return ESP_FAIL;
    }

    s_ctx.mn = esp_mn_handle_from_name(mn_name);
    if (!s_ctx.mn) {
        ESP_LOGE(TAG, "esp_mn_handle_from_name('%s') failed", mn_name);
        return ESP_FAIL;
    }

    // detect_length_ms: keep default-ish 6000ms until tuned
    s_ctx.mn_handle = s_ctx.mn->create(mn_name, 6000);
    if (!s_ctx.mn_handle) {
        ESP_LOGE(TAG, "mn->create('%s',6000) failed", mn_name);
        return ESP_FAIL;
    }

    s_ctx.samp_chunksize = s_ctx.mn->get_samp_chunksize(s_ctx.mn_handle);
    if (s_ctx.samp_chunksize <= 0 || s_ctx.samp_chunksize > 4096) {
        ESP_LOGE(TAG, "invalid samp_chunksize=%d", s_ctx.samp_chunksize);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MultiNet ready: model='%s' samp_chunksize=%d", mn_name, s_ctx.samp_chunksize);

    return mn_register_default_phrases();
}

static void mn_task(void *arg)
{
    (void)arg;

    while (1) {
        xEventGroupWaitBits(s_ctx.eg, EG_BIT_RUN, pdFALSE, pdTRUE, portMAX_DELAY);

        // Allocate buffer sized for MultiNet frame
        const int max_samples = s_ctx.samp_chunksize;
        int16_t *pcm = (int16_t *)heap_caps_malloc((size_t)max_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL);
        if (!pcm) {
            ESP_LOGE(TAG, "no mem for pcm buffer (%d samples)", max_samples);
            xEventGroupClearBits(s_ctx.eg, EG_BIT_RUN);
            continue;
        }

        while ((xEventGroupGetBits(s_ctx.eg) & EG_BIT_RUN) != 0) {

            static uint32_t s_last_alive_ms = 0;
            uint32_t t_ms = now_ms();
            if ((uint32_t)(t_ms - s_last_alive_ms) >= 1000u) {
                s_last_alive_ms = t_ms;
                ESP_LOGI(TAG, "MN: alive (chunksize=%d)", s_ctx.samp_chunksize);
            }


            // timeout guard
            if (s_ctx.deadline_ms != 0 && (int32_t)(now_ms() - s_ctx.deadline_ms) >= 0) {
                emit_result(ASR_CMD_NONE, -1, 0.0f, "timeout");
                xEventGroupClearBits(s_ctx.eg, EG_BIT_RUN);
                break;
            }

            // Accumulate until we have exactly samp_chunksize samples for detect()
            static int fill = 0;

            size_t got = 0;
            esp_err_t err = audio_stream_read_mono_s16(
                &pcm[fill],
                (size_t)(max_samples - fill),
                &got,
                pdMS_TO_TICKS(ASR_MN_READ_TIMEOUT_MS)
            );

            if (err != ESP_OK || got == 0) {
                continue;
            }

            fill += (int)got;

            if (fill < max_samples) {
                // not enough yet, keep accumulating
                continue;
            }

            fill = 0; // ready frame in pcm[0..max_samples-1]

            esp_mn_state_t st = s_ctx.mn->detect(s_ctx.mn_handle, pcm);

            if (st == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *res = s_ctx.mn->get_results(s_ctx.mn_handle);
                if (res && res->num > 0) {
                    ESP_LOGI(TAG, "MN: DETECTED phrase_id=%d prob=%.3f", res->phrase_id[0], res->prob[0]);
                } else {
                    ESP_LOGI(TAG, "MN: DETECTED but empty results");
                }
            } else if (st == ESP_MN_STATE_TIMEOUT) {
                ESP_LOGI(TAG, "MN: TIMEOUT (from detect)");
            }


            if (st == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *res = s_ctx.mn->get_results(s_ctx.mn_handle);
                if (res && res->num > 0) {
                    int phrase_id = res->phrase_id[0];
                    float prob = res->prob[0];

                    asr_cmd_t cmd = map_phrase_to_cmd(phrase_id);
                    emit_result(cmd, phrase_id, prob, NULL);
                } else {
                    emit_result(ASR_CMD_NONE, -1, 0.0f, "detected_empty");
                }

                // single-shot: stop session after one command
                xEventGroupClearBits(s_ctx.eg, EG_BIT_RUN);
                break;
            } else if (st == ESP_MN_STATE_TIMEOUT) {
                emit_result(ASR_CMD_NONE, -1, 0.0f, "mn_timeout");
                xEventGroupClearBits(s_ctx.eg, EG_BIT_RUN);
                break;
            } else {
                // detecting -> continue
            }
        }

        heap_caps_free(pcm);
    }
}

/* ---------- public API ---------- */

esp_err_t asr_multinet_init(asr_multinet_result_cb_t cb, void *user_ctx)
{
    s_ctx.cb = cb;
    s_ctx.cb_user = user_ctx;

    if (s_ctx.eg == NULL) {
        s_ctx.eg = xEventGroupCreate();
        if (!s_ctx.eg) return ESP_ERR_NO_MEM;
    }

    // Load models now (fail-fast)
    esp_err_t err = mn_load_and_create();
    if (err != ESP_OK) {
        mn_cleanup();
        return err;
    }

    if (s_ctx.task == NULL) {
        BaseType_t ok = xTaskCreate(mn_task, "asr_mn", ASR_MN_TASK_STACK, NULL, ASR_MN_TASK_PRIO, &s_ctx.task);
        if (ok != pdPASS) {
            mn_cleanup();
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

void asr_multinet_deinit(void)
{
    (void)asr_multinet_stop_session();

    mn_cleanup();

    if (s_ctx.eg) {
        vEventGroupDelete(s_ctx.eg);
        s_ctx.eg = NULL;
    }

    // task оставляем (можно позже сделать delete при необходимости)
    s_ctx.cb = NULL;
    s_ctx.cb_user = NULL;
}

bool asr_multinet_is_active(void)
{
    if (!s_ctx.eg) return false;
    return (xEventGroupGetBits(s_ctx.eg) & EG_BIT_RUN) != 0;
}

esp_err_t asr_multinet_start_session(uint32_t timeout_ms)
{
    if (!s_ctx.mn_handle || !s_ctx.eg) return ESP_ERR_INVALID_STATE;

    s_ctx.deadline_ms = (timeout_ms > 0) ? (now_ms() + timeout_ms) : 0;
    xEventGroupSetBits(s_ctx.eg, EG_BIT_RUN);
    return ESP_OK;
}

esp_err_t asr_multinet_stop_session(void)
{
    if (!s_ctx.eg) return ESP_OK;
    xEventGroupClearBits(s_ctx.eg, EG_BIT_RUN);
    return ESP_OK;
}
