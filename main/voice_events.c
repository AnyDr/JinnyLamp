#include "voice_events.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "audio_player.h"

static const char *TAG = "VOICE_EVT";

/* NVS: храним "мешок" (played_mask) только для BOOT/GODBYE, чтобы переживать ребут.
   Остальные события: RAM-only (достаточно для “живой” работы). */
#define VOICE_EVT_NVS_NS              "voice_evt"
#define VOICE_EVT_NVS_KEY_BOOT_MASK   "boot_mask"
#define VOICE_EVT_NVS_KEY_BYE_MASK    "bye_mask"

typedef struct {
    voice_evt_t evt;
    const char *p0;
    const char *p1;
    const char *p2;
} voice_evt_map3_t;

/* Правило: если вариант не используется — ставь NULL. */
static const voice_evt_map3_t s_map[] = {
    /* Boot greeting: 1..3 файла. */
    {
        VOICE_EVT_BOOT_GREETING,
        "/spiffs/voice/boot_greeting.pcm",
        "/spiffs/voice/boot_greeting_1.pcm",
        "/spiffs/voice/boot_greeting_2.pcm"
    },

    /* Power-off goodbye: добавишь файлы позже. */
    {
        VOICE_EVT_POWER_OFF_GOODBYE,
        "/spiffs/voice/goodbye_1.pcm",
        "/spiffs/voice/goodbye_2.pcm",
        "/spiffs/voice/goodbye_3.pcm"
    },

    /* Заготовки под будущее:
    {
        VOICE_EVT_THINKING,
        "/spiffs/voice/thinking_1.pcm",
        "/spiffs/voice/thinking_2.pcm",
        "/spiffs/voice/thinking_3.pcm"
    },
    {
        VOICE_EVT_CMD_OK,
        "/spiffs/voice/cmd_ok_1.pcm",
        "/spiffs/voice/cmd_ok_2.pcm",
        "/spiffs/voice/cmd_ok_3.pcm"
    },
    */
};

/* played_mask: 3 младших бита соответствуют p0/p1/p2.
   Для BOOT/GOODBYE — persistent (NVS), для остальных — RAM-only. */
static uint8_t s_played_mask_ram[VOICE_EVT__COUNT];

/* --------- NVS helpers --------- */

static esp_err_t nvs_read_u8(const char *key, uint8_t *out_val)
{
    if (!key || !out_val) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(VOICE_EVT_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t v = 0;
    err = nvs_get_u8(h, key, &v);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_val = 0;
        return ESP_OK;
    }

    if (err == ESP_OK) {
        *out_val = v;
    }
    return err;
}

static esp_err_t nvs_write_u8(const char *key, uint8_t val)
{
    if (!key) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(VOICE_EVT_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(h, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static bool evt_is_persistent(voice_evt_t evt)
{
    return (evt == VOICE_EVT_BOOT_GREETING) || (evt == VOICE_EVT_POWER_OFF_GOODBYE);
}

static const char *evt_key_for_persistent_mask(voice_evt_t evt)
{
    if (evt == VOICE_EVT_BOOT_GREETING) return VOICE_EVT_NVS_KEY_BOOT_MASK;
    if (evt == VOICE_EVT_POWER_OFF_GOODBYE) return VOICE_EVT_NVS_KEY_BYE_MASK;
    return NULL;
}

static uint8_t played_mask_get(voice_evt_t evt)
{
    if (!evt_is_persistent(evt)) {
        return (uint8_t)(s_played_mask_ram[(int)evt] & 0x07u);
    }

    const char *key = evt_key_for_persistent_mask(evt);
    uint8_t v = 0;
    const esp_err_t err = nvs_read_u8(key, &v);
    if (err != ESP_OK) {
        /* Если NVS временно недоступен — деградируем в RAM-only поведение */
        ESP_LOGW(TAG, "nvs_read_u8('%s') err=%s -> fallback RAM", key, esp_err_to_name(err));
        return (uint8_t)(s_played_mask_ram[(int)evt] & 0x07u);
    }
    return (uint8_t)(v & 0x07u);
}

static void played_mask_set(voice_evt_t evt, uint8_t v)
{
    v &= 0x07u;

    if (!evt_is_persistent(evt)) {
        s_played_mask_ram[(int)evt] = v;
        return;
    }

    const char *key = evt_key_for_persistent_mask(evt);
    const esp_err_t err = nvs_write_u8(key, v);
    if (err != ESP_OK) {
        /* Если NVS не пишет — всё равно держим в RAM, чтобы не ломать UX */
        ESP_LOGW(TAG, "nvs_write_u8('%s',0x%02X) err=%s -> keep RAM too",
                 key, (unsigned)v, esp_err_to_name(err));
        s_played_mask_ram[(int)evt] = v;
        return;
    }
}

/* --------- mapping + picking --------- */

static const voice_evt_map3_t *voice_evt_find(voice_evt_t evt)
{
    for (size_t i = 0; i < (sizeof(s_map) / sizeof(s_map[0])); i++) {
        if (s_map[i].evt == evt) {
            return &s_map[i];
        }
    }
    return NULL;
}

static const char *variant_path_3(const voice_evt_map3_t *m, int idx)
{
    switch (idx) {
        case 0: return m->p0;
        case 1: return m->p1;
        case 2: return m->p2;
        default: return NULL;
    }
}

static uint8_t available_mask_3(const voice_evt_map3_t *m)
{
    uint8_t mask = 0;
    if (m->p0) mask |= (1u << 0);
    if (m->p1) mask |= (1u << 1);
    if (m->p2) mask |= (1u << 2);
    return mask;
}

static const char *pick_path_no_repeat_3(voice_evt_t evt, const voice_evt_map3_t *m)
{
    const uint8_t avail = available_mask_3(m);
    if (avail == 0) {
        return NULL;
    }

    uint8_t played = played_mask_get(evt);

    /* Если всё уже было сыграно (по доступным вариантам) — сбрасываем мешок. */
    if ((played & avail) == avail) {
        played = 0;
        played_mask_set(evt, 0);
    }

    const uint8_t remaining = (uint8_t)(avail & (uint8_t)~played);
    const int rem_cnt =
        ((remaining >> 0) & 1u) + ((remaining >> 1) & 1u) + ((remaining >> 2) & 1u);

    if (rem_cnt <= 0) {
        return NULL;
    }

    /* Выбираем k-й установленный бит среди remaining */
    const uint32_t r = esp_random();
    int k = (int)(r % (uint32_t)rem_cnt);

    int chosen_idx = -1;
    for (int i = 0; i < 3; i++) {
        if (remaining & (1u << i)) {
            if (k == 0) {
                chosen_idx = i;
                break;
            }
            k--;
        }
    }

    if (chosen_idx < 0) {
        return NULL;
    }

    const uint8_t new_played = (uint8_t)(played | (1u << chosen_idx));
    played_mask_set(evt, new_played);
    return variant_path_3(m, chosen_idx);
}

/* --------- public API --------- */

esp_err_t voice_events_init(void)
{
    /* RAM masks */
    for (int i = 0; i < (int)VOICE_EVT__COUNT; i++) {
        s_played_mask_ram[i] = 0;
    }

    /* Прогреваем NVS (обычно уже сделано в проекте, но лучше быть аккуратным).
       Если NVS не готов — persistent просто будет деградировать в RAM-only. */
    const esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK && nvs_err != ESP_ERR_NVS_NO_FREE_PAGES && nvs_err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs_flash_init err=%s (persistent masks may be RAM-only)", esp_err_to_name(nvs_err));
        return ESP_OK;
    }

    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Типовой recovery */
        ESP_LOGW(TAG, "nvs needs erase (err=%s) -> erase+init", esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Не обязаны читать тут значения (played_mask_get прочитает при надобности),
       но можно залогировать для отладки. */
    uint8_t b = 0, g = 0;
    (void)nvs_read_u8(VOICE_EVT_NVS_KEY_BOOT_MASK, &b);
    (void)nvs_read_u8(VOICE_EVT_NVS_KEY_BYE_MASK, &g);
    ESP_LOGI(TAG, "persistent masks: boot=0x%02X goodbye=0x%02X", (unsigned)b, (unsigned)g);

    return ESP_OK;
}

esp_err_t voice_event_post(voice_evt_t evt)
{
    if ((int)evt < 0 || evt >= VOICE_EVT__COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    const voice_evt_map3_t *m = voice_evt_find(evt);
    if (!m) {
        ESP_LOGW(TAG, "no mapping for evt=%d", (int)evt);
        return ESP_ERR_NOT_FOUND;
    }

    const char *path = pick_path_no_repeat_3(evt, m);
    if (!path) {
        ESP_LOGW(TAG, "no files for evt=%d", (int)evt);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "evt=%d -> '%s'", (int)evt, path);

    /* Пока PCM s16 mono 16k. Позже заменим на ADPCM API (storage ADPCM). */
    return audio_player_play_pcm_s16_mono_16k(path);
}
