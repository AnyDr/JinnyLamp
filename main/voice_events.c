#include "voice_events.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "audio_player.h"

static const char *TAG = "VOICE_EVT";

/* ============================================================
 * NVS persistent shuffle masks
 * Храним только lifecycle deep/boot события.
 * ============================================================ */

#define VOICE_EVT_NVS_NS                   "voice_evt"
#define VOICE_EVT_NVS_KEY_BOOT_MASK        "boot_mask"
#define VOICE_EVT_NVS_KEY_DEEP_WAKE_MASK   "dw_mask"
#define VOICE_EVT_NVS_KEY_DEEP_BYE_MASK    "ds_mask"


/* ============================================================
 * Mapping: evt -> 3 варианта файлов
 * NULL допустим (вариант отсутствует).
 * ============================================================ */

typedef struct {
    voice_evt_t evt;
    const char *p0;
    const char *p1;
    const char *p2;
} voice_evt_map3_t;


/* ---------------- lifecycle ---------------- */

static const voice_evt_map3_t s_map[] = {

    /* ---- A) Lifecycle / Power ---- */

    {
        VOICE_EVT_BOOT_HELLO,
        "/spiffs/voice/lifecycle/evt_boot_hello__v1.pcm",
        "/spiffs/voice/lifecycle/evt_boot_hello__v2.pcm",
        "/spiffs/voice/lifecycle/evt_boot_hello__v3.pcm"
    },
    {
        VOICE_EVT_DEEP_WAKE_HELLO,
        "/spiffs/voice/lifecycle/evt_deep_wake_hello__v1.pcm",
        "/spiffs/voice/lifecycle/evt_deep_wake_hello__v2.pcm",
        "/spiffs/voice/lifecycle/evt_deep_wake_hello__v3.pcm"
    },
    {
        VOICE_EVT_DEEP_SLEEP_BYE,
        "/spiffs/voice/lifecycle/evt_deep_sleep_bye__v1.pcm",
        "/spiffs/voice/lifecycle/evt_deep_sleep_bye__v2.pcm",
        "/spiffs/voice/lifecycle/evt_deep_sleep_bye__v3.pcm"
    },
    {
        VOICE_EVT_SOFT_ON_HELLO,
        "/spiffs/voice/lifecycle/evt_soft_on_hello__v1.pcm",
        "/spiffs/voice/lifecycle/evt_soft_on_hello__v2.pcm",
        "/spiffs/voice/lifecycle/evt_soft_on_hello__v3.pcm"
    },
    {
        VOICE_EVT_SOFT_OFF_BYE,
        "/spiffs/voice/lifecycle/evt_soft_off_bye__v1.pcm",
        "/spiffs/voice/lifecycle/evt_soft_off_bye__v2.pcm",
        "/spiffs/voice/lifecycle/evt_soft_off_bye__v3.pcm"
    },

    /* ---- B) Session ---- */

    {
        VOICE_EVT_WAKE_DETECTED,
        "/spiffs/voice/session/evt_wake_detected__v1.pcm",
        "/spiffs/voice/session/evt_wake_detected__v2.pcm",
        "/spiffs/voice/session/evt_wake_detected__v3.pcm"
    },
    {
        VOICE_EVT_SESSION_CANCELLED,
        "/spiffs/voice/session/evt_session_cancelled__v1.pcm",
        "/spiffs/voice/session/evt_session_cancelled__v2.pcm",
        "/spiffs/voice/session/evt_session_cancelled__v3.pcm"
    },
    {
        VOICE_EVT_NO_CMD_TIMEOUT,
        "/spiffs/voice/session/evt_no_cmd_timeout__v1.pcm",
        "/spiffs/voice/session/evt_no_cmd_timeout__v2.pcm",
        "/spiffs/voice/session/evt_no_cmd_timeout__v3.pcm"
    },
    {
        VOICE_EVT_BUSY_ALREADY_LISTENING,
        "/spiffs/voice/session/evt_busy_already_listening__v1.pcm",
        "/spiffs/voice/session/evt_busy_already_listening__v2.pcm",
        "/spiffs/voice/session/evt_busy_already_listening__v3.pcm"
    },

    /* ---- C) Command outcomes ---- */

    {
        VOICE_EVT_CMD_OK,
        "/spiffs/voice/cmd/evt_cmd_ok__v1.pcm",
        "/spiffs/voice/cmd/evt_cmd_ok__v2.pcm",
        "/spiffs/voice/cmd/evt_cmd_ok__v3.pcm"
    },
    {
        VOICE_EVT_CMD_FAIL,
        "/spiffs/voice/cmd/evt_cmd_fail__v1.pcm",
        "/spiffs/voice/cmd/evt_cmd_fail__v2.pcm",
        "/spiffs/voice/cmd/evt_cmd_fail__v3.pcm"
    },
    {
        VOICE_EVT_CMD_UNSUPPORTED,
        "/spiffs/voice/cmd/evt_cmd_unsupported__v1.pcm",
        "/spiffs/voice/cmd/evt_cmd_unsupported__v2.pcm",
        "/spiffs/voice/cmd/evt_cmd_unsupported__v3.pcm"
    },

    /* ---- D) Server ---- */

    {
        VOICE_EVT_NEED_THINKING_SERVER,
        "/spiffs/voice/server/evt_need_thinking_server__v1.pcm",
        "/spiffs/voice/server/evt_need_thinking_server__v2.pcm",
        "/spiffs/voice/server/evt_need_thinking_server__v3.pcm"
    },
    {
        VOICE_EVT_SERVER_UNAVAILABLE,
        "/spiffs/voice/server/evt_server_unavailable__v1.pcm",
        "/spiffs/voice/server/evt_server_unavailable__v2.pcm",
        "/spiffs/voice/server/evt_server_unavailable__v3.pcm"
    },
    {
        VOICE_EVT_SERVER_TIMEOUT,
        "/spiffs/voice/server/evt_server_timeout__v1.pcm",
        "/spiffs/voice/server/evt_server_timeout__v2.pcm",
        "/spiffs/voice/server/evt_server_timeout__v3.pcm"
    },
    {
        VOICE_EVT_SERVER_ERROR,
        "/spiffs/voice/server/evt_server_error__v1.pcm",
        "/spiffs/voice/server/evt_server_error__v2.pcm",
        "/spiffs/voice/server/evt_server_error__v3.pcm"
    },

    /* ---- E) OTA ---- */

    {
        VOICE_EVT_OTA_ENTER,
        "/spiffs/voice/ota/evt_ota_enter__v1.pcm",
        "/spiffs/voice/ota/evt_ota_enter__v2.pcm",
        "/spiffs/voice/ota/evt_ota_enter__v3.pcm"
    },
    {
        VOICE_EVT_OTA_OK,
        "/spiffs/voice/ota/evt_ota_ok__v1.pcm",
        "/spiffs/voice/ota/evt_ota_ok__v2.pcm",
        "/spiffs/voice/ota/evt_ota_ok__v3.pcm"
    },
    {
        VOICE_EVT_OTA_FAIL,
        "/spiffs/voice/ota/evt_ota_fail__v1.pcm",
        "/spiffs/voice/ota/evt_ota_fail__v2.pcm",
        "/spiffs/voice/ota/evt_ota_fail__v3.pcm"
    },
    {
        VOICE_EVT_OTA_TIMEOUT,
        "/spiffs/voice/ota/evt_ota_timeout__v1.pcm",
        "/spiffs/voice/ota/evt_ota_timeout__v2.pcm",
        "/spiffs/voice/ota/evt_ota_timeout__v3.pcm"
    },

    /* ---- F) Errors ---- */

    {
        VOICE_EVT_ERR_GENERIC,
        "/spiffs/voice/error/evt_err_generic__v1.pcm",
        "/spiffs/voice/error/evt_err_generic__v2.pcm",
        "/spiffs/voice/error/evt_err_generic__v3.pcm"
    },
    {
        VOICE_EVT_ERR_STORAGE,
        "/spiffs/voice/error/evt_err_storage__v1.pcm",
        "/spiffs/voice/error/evt_err_storage__v2.pcm",
        "/spiffs/voice/error/evt_err_storage__v3.pcm"
    },
    {
        VOICE_EVT_ERR_AUDIO,
        "/spiffs/voice/error/evt_err_audio__v1.pcm",
        "/spiffs/voice/error/evt_err_audio__v2.pcm",
        "/spiffs/voice/error/evt_err_audio__v3.pcm"
    },
};


/* ============================================================
 * RAM shuffle masks — размер = VOICE_EVT__COUNT
 * ============================================================ */

static uint8_t s_played_mask_ram[VOICE_EVT__COUNT];


/* ============================================================
 * Persistent policy — только lifecycle deep/boot
 * ============================================================ */

static bool evt_is_persistent(voice_evt_t evt)
{
    switch (evt) {
        case VOICE_EVT_BOOT_HELLO:
        case VOICE_EVT_DEEP_WAKE_HELLO:
        case VOICE_EVT_DEEP_SLEEP_BYE:
            return true;
        default:
            return false;
    }
}

static const char *evt_key_for_persistent_mask(voice_evt_t evt)
{
    switch (evt) {
        case VOICE_EVT_BOOT_HELLO:       return VOICE_EVT_NVS_KEY_BOOT_MASK;
        case VOICE_EVT_DEEP_WAKE_HELLO:  return VOICE_EVT_NVS_KEY_DEEP_WAKE_MASK;
        case VOICE_EVT_DEEP_SLEEP_BYE:   return VOICE_EVT_NVS_KEY_DEEP_BYE_MASK;
        default: return NULL;
    }
}


/* -------- NVS helpers -------- */

static esp_err_t nvs_read_u8(const char *key, uint8_t *out_val)
{
    if (!key || !out_val) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(VOICE_EVT_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t v = 0;
    err = nvs_get_u8(h, key, &v);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_val = 0;
        return ESP_OK;
    }
    if (err == ESP_OK) *out_val = v;
    return err;
}

static esp_err_t nvs_write_u8(const char *key, uint8_t val)
{
    if (!key) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(VOICE_EVT_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}


/* -------- shuffle mask get/set -------- */

static uint8_t played_mask_get(voice_evt_t evt)
{
    if (!evt_is_persistent(evt)) {
        return s_played_mask_ram[evt] & 0x07u;
    }

    const char *key = evt_key_for_persistent_mask(evt);
    uint8_t v = 0;
    if (nvs_read_u8(key, &v) != ESP_OK) {
        return s_played_mask_ram[evt] & 0x07u;
    }
    return v & 0x07u;
}

static void played_mask_set(voice_evt_t evt, uint8_t v)
{
    v &= 0x07u;

    if (!evt_is_persistent(evt)) {
        s_played_mask_ram[evt] = v;
        return;
    }

    const char *key = evt_key_for_persistent_mask(evt);
    if (nvs_write_u8(key, v) != ESP_OK) {
        s_played_mask_ram[evt] = v;
    }
}


/* ============================================================
 * Mapping helpers
 * ============================================================ */

static const voice_evt_map3_t *voice_evt_find(voice_evt_t evt)
{
    for (size_t i = 0; i < sizeof(s_map)/sizeof(s_map[0]); i++) {
        if (s_map[i].evt == evt) return &s_map[i];
    }
    return NULL;
}

static uint8_t available_mask_3(const voice_evt_map3_t *m)
{
    uint8_t mask = 0;
    if (m->p0) mask |= 1u << 0;
    if (m->p1) mask |= 1u << 1;
    if (m->p2) mask |= 1u << 2;
    return mask;
}

static const char *variant_path_3(const voice_evt_map3_t *m, int idx)
{
    return (idx==0)?m->p0 : (idx==1)?m->p1 : (idx==2)?m->p2 : NULL;
}


/* ============================================================
 * Shuffle-bag pick (циклический, без ошибок при исчерпании)
 * ============================================================ */

static const char *pick_path_no_repeat_3(voice_evt_t evt, const voice_evt_map3_t *m)
{
    const uint8_t avail = available_mask_3(m);
    if (!avail) return NULL;

    uint8_t played = played_mask_get(evt);

    if ((played & avail) == avail) {
        played = 0;
        played_mask_set(evt, 0);
    }

    const uint8_t remaining = avail & ~played;
    int cnt = ((remaining>>0)&1)+((remaining>>1)&1)+((remaining>>2)&1);
    if (cnt <= 0) return NULL;

    int k = esp_random() % cnt;

    int idx = -1;
    for (int i=0;i<3;i++) {
        if (remaining & (1u<<i)) {
            if (k-- == 0) { idx=i; break; }
        }
    }

    if (idx < 0) return NULL;

    played_mask_set(evt, played | (1u<<idx));
    return variant_path_3(m, idx);
}


/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t voice_events_init(void)
{
    for (int i=0;i<VOICE_EVT__COUNT;i++) s_played_mask_ram[i]=0;
    ESP_LOGI(TAG, "voice events init, count=%d", VOICE_EVT__COUNT);
    return ESP_OK;
}

esp_err_t voice_event_post(voice_evt_t evt)
{
    const voice_evt_map3_t *m = voice_evt_find(evt);
    if (!m) {
        ESP_LOGW(TAG, "no mapping for evt=%d", evt);
        return ESP_ERR_NOT_FOUND;
    }

    const char *path = pick_path_no_repeat_3(evt, m);
    if (!path) {
        ESP_LOGW(TAG, "no file for evt=%d", evt);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "evt=%d -> %s", evt, path);
    return audio_player_play_pcm_s16_mono_16k(path);
}
