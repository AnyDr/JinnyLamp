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
 * ============================================================ */

#define VOICE_EVT_NVS_NS                   "voice_evt"
#define VOICE_EVT_NVS_KEY_BOOT_MASK        "boot_mask"
#define VOICE_EVT_NVS_KEY_DEEP_WAKE_MASK   "dw_mask"
#define VOICE_EVT_NVS_KEY_DEEP_BYE_MASK    "ds_mask"


/* ============================================================
 * Mapping: evt -> 3 variants (SHORT NAMES, ADPCM WAV)
 * SPIFFS v2 layout: /spiffs/v/<grp>/<grp-XX-VV.wav>
 * ============================================================ */

typedef struct {
    voice_evt_t evt;
    const char *p0;
    const char *p1;
    const char *p2;
} voice_evt_map3_t;


/* ---------------- lifecycle (lc) ---------------- */

static const voice_evt_map3_t s_map[] = {

    { VOICE_EVT_BOOT_HELLO, // после подачи питания на плату
      "/spiffs/v/lc/lc-01-01.wav",
      "/spiffs/v/lc/lc-01-02.wav",
      "/spiffs/v/lc/lc-01-03.wav" },

    { VOICE_EVT_DEEP_WAKE_HELLO, //после выхода из диип сли
      "/spiffs/v/lc/lc-02-01.wav",
      "/spiffs/v/lc/lc-02-02.wav",
      "/spiffs/v/lc/lc-02-03.wav" },

    { VOICE_EVT_DEEP_SLEEP_BYE, //вход в дип слип
      "/spiffs/v/lc/lc-03-01.wav",
      "/spiffs/v/lc/lc-03-02.wav",
      "/spiffs/v/lc/lc-03-03.wav" },

    { VOICE_EVT_SOFT_ON_HELLO, //выход из софт слип
      "/spiffs/v/lc/lc-04-01.wav",
      "/spiffs/v/lc/lc-04-02.wav",
      "/spiffs/v/lc/lc-04-03.wav" },

    { VOICE_EVT_SOFT_OFF_BYE, //вход в софт слип
      "/spiffs/v/lc/lc-05-01.wav",
      "/spiffs/v/lc/lc-05-02.wav",
      "/spiffs/v/lc/lc-05-03.wav" },

/* ---------------- session (ss) ---------------- */

    { VOICE_EVT_WAKE_DETECTED, //вейк ворд распознан
      "/spiffs/v/ss/ss-01-01.wav",
      "/spiffs/v/ss/ss-01-02.wav",
      "/spiffs/v/ss/ss-01-03.wav" },

    { VOICE_EVT_SESSION_CANCELLED, //сессия после вейк ворда отменна пользователем голосом
      "/spiffs/v/ss/ss-02-01.wav",
      "/spiffs/v/ss/ss-02-02.wav",
      "/spiffs/v/ss/ss-02-03.wav" },

    { VOICE_EVT_NO_CMD_TIMEOUT, // сессия после вейк ворд истекла по таймеру
      "/spiffs/v/ss/ss-03-01.wav",
      "/spiffs/v/ss/ss-03-02.wav",
      "/spiffs/v/ss/ss-03-03.wav" },

    { VOICE_EVT_BUSY_ALREADY_LISTENING, // сессия вейк вор уже активна
      "/spiffs/v/ss/ss-04-01.wav",
      "/spiffs/v/ss/ss-04-02.wav",
      "/spiffs/v/ss/ss-04-03.wav" },

/* ---------------- command (cmd) ---------------- */

    { VOICE_EVT_CMD_OK, // подтверждение успешной команды
      "/spiffs/v/cmd/cmd-01-01.wav",
      "/spiffs/v/cmd/cmd-01-02.wav",
      "/spiffs/v/cmd/cmd-01-03.wav" },

    { VOICE_EVT_CMD_FAIL, //команда зафейленапо каким то причинам
      "/spiffs/v/cmd/cmd-02-01.wav",
      "/spiffs/v/cmd/cmd-02-02.wav",
      "/spiffs/v/cmd/cmd-02-03.wav" },

    { VOICE_EVT_CMD_UNSUPPORTED, //команда зафейлена по причине отсутствия фукционала
      "/spiffs/v/cmd/cmd-03-01.wav",
      "/spiffs/v/cmd/cmd-03-02.wav",
      "/spiffs/v/cmd/cmd-03-03.wav" },

/* ---------------- server (srv) ---------------- */

    { VOICE_EVT_NEED_THINKING_SERVER, //таймаут на подумать при пересылке данных на сервер на взрослую LLM
      "/spiffs/v/srv/srv-01-01.wav",
      "/spiffs/v/srv/srv-01-02.wav",
      "/spiffs/v/srv/srv-01-03.wav" },

    { VOICE_EVT_SERVER_UNAVAILABLE, //Сервер офлайн
      "/spiffs/v/srv/srv-02-01.wav",
      "/spiffs/v/srv/srv-02-02.wav",
      "/spiffs/v/srv/srv-02-03.wav" },

    { VOICE_EVT_SERVER_TIMEOUT, // Таймаут ответа от сервера превышен
      "/spiffs/v/srv/srv-03-01.wav",
      "/spiffs/v/srv/srv-03-02.wav",
      "/spiffs/v/srv/srv-03-03.wav" },

    { VOICE_EVT_SERVER_ERROR, //Сервер шлет в ответ какую то дичь
      "/spiffs/v/srv/srv-04-01.wav",
      NULL,
      NULL },

/* ---------------- ota (ota) ---------------- */

    { VOICE_EVT_OTA_ENTER,
      "/spiffs/v/ota/ota-01-01.wav", //Вход в режим ОТА, поднятие вай фай точки
      NULL, NULL },

    { VOICE_EVT_OTA_OK, //ОТА загрузился ок, перезагрузка
      "/spiffs/v/ota/ota-02-01.wav",
      NULL, NULL },

    { VOICE_EVT_OTA_FAIL, //ОТА не записалось в слот
      "/spiffs/v/ota/ota-03-01.wav",
      NULL, NULL },

    { VOICE_EVT_OTA_TIMEOUT, //Истечение времения таймаута  для ОТА загрузки
      "/spiffs/v/ota/ota-04-01.wav",
      NULL, NULL },

/* ---------------- error (err) ---------------- */

    { VOICE_EVT_ERR_GENERIC, //общая ошибка чего то
      "/spiffs/v/err/err-01-01.wav",
      NULL, NULL },

    { VOICE_EVT_ERR_STORAGE, //Ошибка хранилища
      "/spiffs/v/err/err-02-01.wav",
      NULL, NULL },

    { VOICE_EVT_ERR_AUDIO, //ошибка аудио плеера
      "/spiffs/v/err/err-03-01.wav",
      NULL, NULL },
};


/* ============================================================
 * RAM shuffle masks
 * ============================================================ */

static uint8_t s_played_mask_ram[VOICE_EVT__COUNT];


/* ============================================================
 * Persistent policy — unchanged
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


/* -------- helpers unchanged below -------- */

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

static const char *pick_path_no_repeat_3(voice_evt_t evt, const voice_evt_map3_t *m)
{
    const uint8_t avail = available_mask_3(m);
    if (!avail) return NULL;

    uint8_t played = s_played_mask_ram[evt] & 0x07u;

    if ((played & avail) == avail) {
        played = 0;
        s_played_mask_ram[evt] = 0;
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

    s_played_mask_ram[evt] |= (1u<<idx);
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

    /* теперь WAV ADPCM, но API пока то же — адаптируешь в audio_player */
    return audio_player_play_pcm_s16_mono_16k(path);
}
