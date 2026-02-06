#include "wake_wakenet.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_err.h"


/* ESP-SR */
#include "model_path.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"



/* ---- state ---- */
static const char *TAG = "WAKE_WN";

static srmodel_list_t *s_models = NULL;
static char s_model_name[64] = {0};

static const esp_wn_iface_t *s_wn = NULL;
static model_iface_data_t *s_wn_data = NULL;

static wake_wakenet_info_t s_info = {
    .wn_model_name = NULL,
    .sample_rate_hz = 0,
};

esp_err_t wake_wakenet_init(void)
{
    if (s_wn_data) {
        return ESP_OK; // already inited
    }

    /* 1) load model list from partition "model" (flash mmap) */
    s_models = esp_srmodel_init("model");
    if (!s_models) {
        ESP_LOGE(TAG, "esp_srmodel_init('model') failed (partition missing? model not flashed?)");
        return ESP_FAIL;
    }

    /* 2) pick a WakeNet model name */
    char *name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (!name) {
        ESP_LOGE(TAG, "no wakenet model found in 'model' partition (ESP_WN_PREFIX)");
        return ESP_ERR_NOT_FOUND;
    }

    strlcpy(s_model_name, name, sizeof(s_model_name));
    s_info.wn_model_name = s_model_name;

    /* 3) get wakenet iface and create handle */
    s_wn = esp_wn_handle_from_name(s_model_name);
    if (!s_wn) {
        ESP_LOGE(TAG, "esp_wn_handle_from_name('%s') failed", s_model_name);
        return ESP_FAIL;
    }

    s_wn_data = s_wn->create(s_model_name, DET_MODE_95);
    if (!s_wn_data) {
        ESP_LOGE(TAG, "wakenet->create('%s', DET_MODE_95) failed", s_model_name);
        return ESP_FAIL;
    }

    s_info.sample_rate_hz = s_wn->get_samp_rate(s_wn_data);

    ESP_LOGI(TAG, "WakeNet init OK: model='%s' samp_rate=%d Hz",
             s_model_name, s_info.sample_rate_hz);

    /* На этом шаге мы НЕ запускаем detect task. Только проверили, что модели читаются и create работает. */
    return ESP_OK;
}

void wake_wakenet_deinit(void)
{
    if (s_wn && s_wn_data) {
        s_wn->destroy(s_wn_data);
    }
    s_wn_data = NULL;
    s_wn = NULL;

    /* esp_srmodel_deinit() в публичных примерах часто не используется,
       но если у тебя есть — можно добавить. Сейчас оставляем как есть. */
    s_models = NULL;

    memset(s_model_name, 0, sizeof(s_model_name));
    s_info.wn_model_name = NULL;
    s_info.sample_rate_hz = 0;
}

void wake_wakenet_get_info(wake_wakenet_info_t *out)
{
    if (!out) return;
    *out = s_info;
}

bool wake_wakenet_detect(const int16_t *pcm, int samples)
{
    if (!s_wn || !s_wn_data || !pcm || samples <= 0) return false;

    /* ВНИМАНИЕ: если компилятор ругнётся на сигнатуру detect —
       пришлёшь ошибку, я дам точную замену под твою версию esp-sr. */
    int r = s_wn->detect(s_wn_data, (int16_t *)pcm);
    return (r > 0);
}
