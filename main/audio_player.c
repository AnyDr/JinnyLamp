#include "audio_player.h"

/*
 * audio_player.c
 *
 * Минимальный тестовый “плеер” для SPIFFS/LittleFS:
 *   - формат входного файла: PCM s16 mono @ 16 kHz (raw .pcm)
 *   - на выход: I2S TX 32-bit stereo @ 16 kHz (дублируем mono в L/R, left-aligned)
 *
 * Инварианты/цели:
 *   - single-flight: одновременно играет только один файл (защита семафором)
 *   - безопасный stop: по завершению “промываем” тишиной и выключаем TX
 *   - без падений: избегаем stack overflow и коррапта памяти
 *
 * Заметка:
 *   audio_i2s_tx_set_enabled(true/false) желательно делать идемпотентным в audio_i2s.c,
 *   но здесь мы тоже аккуратно игнорируем ESP_ERR_INVALID_STATE.
 */

#include <string.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "audio_i2s.h"

static const char *TAG = "AUDIO_PLAYER";

/* --- Config --- */

#define AUDIO_PLAYER_TASK_CORE          (0)
#define AUDIO_PLAYER_TASK_PRIO          (10)
/* Важно: при локальных буферах на стеке можно легко переполнить. Здесь stack с запасом. */
#define AUDIO_PLAYER_TASK_STACK_BYTES   (8192)

/* Читаем чанками по 256 семплов:
 *  256 * int16 = 512 bytes вход
 *  256 * 2ch * int32 = 2048 bytes выход
 */
#define AUDIO_PLAYER_CHUNK_SAMPLES      (256)

/* Максимальная длина пути, включая '\0'. */
#define AUDIO_PLAYER_PATH_MAX           (128)

/* --- State --- */

/* Single-flight “play” gate: binary semaphore (можно give из player_task). */
static SemaphoreHandle_t s_play_sem = NULL;

static TaskHandle_t s_player_task = NULL;
static volatile bool s_stop = false;
static volatile uint8_t s_volume_pct = 100;  // громкость от 0..100


typedef struct {
    char path[AUDIO_PLAYER_PATH_MAX];
} play_req_t;

/* Буферы вынесены из стека в static: у нас single-flight, одновременно один player_task. */
static int16_t s_in_s16[AUDIO_PLAYER_CHUNK_SAMPLES];
static int32_t s_out_i2s[AUDIO_PLAYER_CHUNK_SAMPLES * 2u]; /* stereo L+R */

static inline int16_t apply_gain_s16(int16_t x, uint8_t vol_pct)
{
    if (vol_pct >= 100) return x;
    if (vol_pct == 0) return 0;

    /* Q15 gain: 0..32767 */
    const int32_t g = (int32_t)((vol_pct * 32767u) / 100u);
    int32_t y = ((int32_t)x * g) >> 15;

    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    return (int16_t)y;
}

static inline int32_t s16_to_i2s_word(int16_t s16)
{
    const uint8_t v = s_volume_pct;
    const int16_t s = apply_gain_s16(s16, v);
    return ((int32_t)s) << 16;
}


static inline void tx_set_enabled_best_effort(bool en)
{
    const esp_err_t err = audio_i2s_tx_set_enabled(en);
    if (err == ESP_OK) {
        return;
    }
    /* Частый кейс: TX уже включён/выключен. Это не должно валить плеер. */
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "tx_set_enabled(%d): already in that state", (int)en);
        return;
    }
    ESP_LOGW(TAG, "tx_set_enabled(%d) err=%s", (int)en, esp_err_to_name(err));
}

static bool path_is_printable_ascii(const char *s)
{
    if (!s || !s[0]) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p > 0x7E) {
            return false;
        }
    }
    return true;
}

static void player_cleanup(play_req_t *req)
{
    /*
     * TX always enabled policy:
     * - TX канал НЕ выключаем (он поднят в audio_i2s_init()).
     * - После playback обязательно “промываем” достаточно длинной тишиной,
     *   чтобы перезаписать весь DMA ring и убрать повторяющиеся хвосты тона.
     */
    (void)audio_i2s_tx_write_silence_ms(500, pdMS_TO_TICKS(1500));

    xSemaphoreGive(s_play_sem);

    vPortFree(req);
    s_player_task = NULL;

    vTaskDelete(NULL);
}



static void player_task(void *arg)
{
    play_req_t *req = (play_req_t *)arg;

    ESP_LOGI(TAG, "play start: '%s'", req->path);
    ESP_LOGI(TAG, "stack HWM=%lu words", (unsigned long)uxTaskGetStackHighWaterMark(NULL));

    /* Базовый предохранитель от коррапта/мусора в path (видели '/spiffs/ ☺♠' в логе). */
    if (!path_is_printable_ascii(req->path)) {
        ESP_LOGE(TAG, "invalid path (non-ascii or empty) -> abort");
        player_cleanup(req);
        return;
    }

    /* Включаем TX перед проигрыванием (идемпотентно). 
    tx_set_enabled_best_effort(true);*/

    FILE *f = fopen(req->path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen failed: %s (errno=%d)", req->path, errno);
        player_cleanup(req);
        return;
    }

    /* PCM s16 mono 16 kHz, raw.
       Пишем в I2S блоками, с аккуратным handling TIMEOUT. */
    const TickType_t write_timeout = pdMS_TO_TICKS(1000);
    int consecutive_timeouts = 0;

    while (!s_stop) {
        const size_t n = fread(s_in_s16, sizeof(s_in_s16[0]), AUDIO_PLAYER_CHUNK_SAMPLES, f);
        if (n == 0) {
            break; /* EOF */
        }

        /* mono -> stereo int32 */
        for (size_t i = 0; i < n; i++) {
            const int32_t w = s16_to_i2s_word(s_in_s16[i]);
            s_out_i2s[i * 2u + 0u] = w;
            s_out_i2s[i * 2u + 1u] = w;
        }

        const size_t bytes_total = n * 2u * sizeof(int32_t);
        size_t off = 0;

        while (off < bytes_total && !s_stop) {
            size_t written = 0;
            const esp_err_t err = audio_i2s_write(
                (const int32_t *)((const uint8_t *)s_out_i2s + off),
                bytes_total - off,
                &written,
                write_timeout);

            if (err == ESP_OK) {
                off += written;
                consecutive_timeouts = 0;
                continue;
            }

            /* TIMEOUT с частичной записью: двигаемся и пробуем дальше */
            if (err == ESP_ERR_TIMEOUT && written > 0) {
                ESP_LOGW(TAG, "i2s_write timeout (partial): written=%u/%u",
                         (unsigned)written, (unsigned)(bytes_total - off));
                off += written;
            } else {
                ESP_LOGW(TAG, "i2s_write err=%s written=%u/%u",
                         esp_err_to_name(err),
                         (unsigned)written, (unsigned)(bytes_total - off));
            }

            consecutive_timeouts++;

            /* Предохранитель: если TX реально “залип”, прекращаем, чтобы не зависать. */
            if (consecutive_timeouts >= 3) {
                ESP_LOGE(TAG, "too many TX timeouts -> abort playback");
                s_stop = true;
                break;
            }
        }
    }

    fclose(f);

    ESP_LOGI(TAG, "play done (stop=%d)", (int)s_stop);
    player_cleanup(req);
}

esp_err_t audio_player_init(void)
{
    if (!s_play_sem) {
        s_play_sem = xSemaphoreCreateBinary();
        if (!s_play_sem) {
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(s_play_sem); /* initially available */
    }
    return ESP_OK;
}

esp_err_t audio_player_play_pcm_s16_mono_16k(const char *path)
{
    if (!s_play_sem) {
        const esp_err_t err = audio_player_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    /* single-flight */
    if (xSemaphoreTake(s_play_sem, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    s_stop = false;

    play_req_t *req = (play_req_t *)pvPortMalloc(sizeof(play_req_t));
    if (!req) {
        xSemaphoreGive(s_play_sem);
        return ESP_ERR_NO_MEM;
    }

    memset(req, 0, sizeof(*req));
    strlcpy(req->path, path, sizeof(req->path));

    BaseType_t ok = xTaskCreatePinnedToCore(
        player_task,
        "audio_player",
        AUDIO_PLAYER_TASK_STACK_BYTES,
        req,
        AUDIO_PLAYER_TASK_PRIO,
        &s_player_task,
        AUDIO_PLAYER_TASK_CORE);

    if (ok != pdPASS) {
        vPortFree(req);
        xSemaphoreGive(s_play_sem);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void audio_player_stop(void)
{
    s_stop = true;
}

void audio_player_set_volume_pct(uint8_t vol_pct)
{
    if (vol_pct > 100) vol_pct = 100;
    s_volume_pct = vol_pct;
}

uint8_t audio_player_get_volume_pct(void)
{
    return s_volume_pct;
}
