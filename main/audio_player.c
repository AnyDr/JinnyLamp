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

/* Для IMA ADPCM блоки часто дают ~505 samples (при block_align=256),
 * поэтому делаем чанк 512, чтобы не дробить лишний раз. */
#define AUDIO_PLAYER_CHUNK_SAMPLES      (512)


/* Максимальная длина пути, включая '\0'. */
#define AUDIO_PLAYER_PATH_MAX           (128)

/* --- State --- */

/* Single-flight “play” gate: binary semaphore (можно give из player_task). */
static SemaphoreHandle_t s_play_sem = NULL;

static TaskHandle_t s_player_task = NULL;
static volatile bool s_stop = false;
static volatile uint8_t s_volume_pct = 100;  // громкость от 0..100
static audio_player_done_cb_t s_done_cb = NULL;
static void *s_done_cb_arg = NULL;



typedef struct {
    char path[AUDIO_PLAYER_PATH_MAX];
} play_req_t;

/* Буферы вынесены из стека в static: у нас single-flight, одновременно один player_task. */
static int16_t s_in_s16[AUDIO_PLAYER_CHUNK_SAMPLES];
static int32_t s_out_i2s[AUDIO_PLAYER_CHUNK_SAMPLES * 2u]; /* stereo L+R */

/* ============================================================
 * WAV (RIFF) + IMA ADPCM decoder (mono)
 * ============================================================ */

static inline uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)((uint32_t)p[0] |
                      ((uint32_t)p[1] << 8) |
                      ((uint32_t)p[2] << 16) |
                      ((uint32_t)p[3] << 24));
}

typedef struct {
    bool     is_wav;
    bool     is_ima_adpcm;

    uint16_t channels;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    /* IMA ADPCM specific */
    uint16_t samples_per_block;

    /* data chunk */
    uint32_t data_offset;
    uint32_t data_size;
} wav_info_t;

/* return ESP_OK if WAV parsed; ESP_ERR_NOT_SUPPORTED if not a WAV */
static esp_err_t wav_parse(FILE *f, wav_info_t *out)
{
    memset(out, 0, sizeof(*out));

    uint8_t hdr[12];
    if (fseek(f, 0, SEEK_SET) != 0) return ESP_FAIL;
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) return ESP_ERR_NOT_SUPPORTED;

    if (memcmp(hdr + 0, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->is_wav = true;

    bool have_fmt = false;
    bool have_data = false;

    /* Walk chunks */
    while (1) {
        uint8_t chdr[8];
        if (fread(chdr, 1, sizeof(chdr), f) != sizeof(chdr)) break;

        const uint32_t ck_size = rd_le32(chdr + 4);
        const long ck_data_pos = ftell(f);
        if (ck_data_pos < 0) return ESP_FAIL;

        if (memcmp(chdr + 0, "fmt ", 4) == 0) {
            uint8_t fmt[32];
            const uint32_t need = (ck_size < sizeof(fmt)) ? ck_size : (uint32_t)sizeof(fmt);
            memset(fmt, 0, sizeof(fmt));
            if (fread(fmt, 1, need, f) != need) return ESP_FAIL;

            const uint16_t format_tag   = rd_le16(fmt + 0);
            out->channels              = rd_le16(fmt + 2);
            out->sample_rate           = rd_le32(fmt + 4);
            out->block_align           = rd_le16(fmt + 12);
            out->bits_per_sample       = rd_le16(fmt + 14);

            out->is_ima_adpcm = (format_tag == 0x0011);

            /* For IMA ADPCM WAV: extension contains samples_per_block (usually at fmt+18..19) */
            if (out->is_ima_adpcm) {
                if (ck_size >= 20) {
                    /* fmt layout: ... bits_per_sample(2), cbSize(2), samplesPerBlock(2) ... */
                    /* In many files: cbSize at 16, samplesPerBlock at 18 */
                    const uint16_t cbSize = (ck_size >= 18) ? rd_le16(fmt + 16) : 0;
                    (void)cbSize;
                    out->samples_per_block = (ck_size >= 20) ? rd_le16(fmt + 18) : 0;
                }
                /* If not present, derive from block_align (mono): ((block_align - 4) * 2) + 1 */
                if (out->samples_per_block == 0 && out->block_align >= 4 && out->channels == 1) {
                    out->samples_per_block = (uint16_t)(((out->block_align - 4u) * 2u) + 1u);
                }
            }

            have_fmt = true;
        }
        else if (memcmp(chdr + 0, "data", 4) == 0) {
            out->data_offset = (uint32_t)ck_data_pos;
            out->data_size   = ck_size;
            have_data = true;
        }

        /* seek to next chunk (chunks are padded to even size) */
        long next = ck_data_pos + (long)ck_size;
        if (ck_size & 1u) next += 1;
        if (fseek(f, next, SEEK_SET) != 0) break;
    }

    if (!have_fmt || !have_data) {
        return ESP_FAIL;
    }

    /* Validate minimal constraints we need */
    if (out->channels != 1) {
        ESP_LOGE(TAG, "WAV: only mono supported, channels=%u", (unsigned)out->channels);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (out->sample_rate != 16000) {
        ESP_LOGE(TAG, "WAV: only 16kHz supported, sr=%u", (unsigned)out->sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (out->is_ima_adpcm) {
        if (out->block_align < 4 || out->samples_per_block < 2) {
            ESP_LOGE(TAG, "WAV IMA: invalid block_align=%u samples_per_block=%u",
                     (unsigned)out->block_align, (unsigned)out->samples_per_block);
            return ESP_FAIL;
        }
        /* prevent absurd sizes */
        if (out->block_align > 1024 || out->samples_per_block > 2048) {
            ESP_LOGE(TAG, "WAV IMA: block too big: align=%u spb=%u",
                     (unsigned)out->block_align, (unsigned)out->samples_per_block);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }

    return ESP_OK;
}

/* IMA ADPCM step table / index table */
static const int s_ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int s_ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static inline int16_t clamp_s16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/* Decode one IMA ADPCM mono block into dst_s16.
 * dst capacity must be >= samples_per_block. Returns samples written or 0 on error.
 */
static size_t ima_adpcm_decode_block_mono(const uint8_t *blk,
                                         size_t blk_bytes,
                                         int16_t *dst_s16,
                                         size_t dst_cap,
                                         uint16_t samples_per_block)
{
    if (blk_bytes < 4 || dst_cap < samples_per_block || samples_per_block < 2) {
        return 0;
    }

    int predictor = (int16_t)rd_le16(blk + 0);
    int index = (int)blk[2];
    if (index < 0) index = 0;
    if (index > 88) index = 88;

    dst_s16[0] = (int16_t)predictor;

    size_t out_i = 1;
    size_t in_i = 4;

    while (out_i < samples_per_block && in_i < blk_bytes) {
        const uint8_t b = blk[in_i++];

        /* low nibble then high nibble */
        for (int nib = 0; nib < 2 && out_i < samples_per_block; nib++) {
            const int code = (nib == 0) ? (b & 0x0F) : ((b >> 4) & 0x0F);

            int step = s_ima_step_table[index];
            int diff = step >> 3;
            if (code & 1) diff += step >> 2;
            if (code & 2) diff += step >> 1;
            if (code & 4) diff += step;
            if (code & 8) diff = -diff;

            predictor += diff;
            predictor = (int)clamp_s16(predictor);

            index += s_ima_index_table[code & 0x0F];
            if (index < 0) index = 0;
            if (index > 88) index = 88;

            dst_s16[out_i++] = (int16_t)predictor;
        }
    }

    /* If we didn't produce the expected amount, treat as error */
    if (out_i != samples_per_block) {
        return 0;
    }
    return out_i;
}


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

static void player_cleanup(play_req_t *req, audio_player_done_reason_t reason)
{
    /*
     * TX always enabled policy:
     * - TX канал НЕ выключаем (он поднят в audio_i2s_init()).
     * - После playback обязательно “промываем” достаточно длинной тишиной,
     *   чтобы перезаписать весь DMA ring и убрать повторяющиеся хвосты тона.
     */
    (void)audio_i2s_tx_write_silence_ms(500, pdMS_TO_TICKS(1500));

    /* --- DONE callback (из контекста player_task) --- */
    if (s_done_cb) {
        s_done_cb(req->path, reason, s_done_cb_arg);
    }
    /* --- end callback --- */

    xSemaphoreGive(s_play_sem);

    vPortFree(req);
    s_player_task = NULL;

    vTaskDelete(NULL);
}




static void player_task(void *arg)
{
    play_req_t *req = (play_req_t *)arg;
    bool aborted_error = false;

    ESP_LOGI(TAG, "play start: '%s'", req->path);
    ESP_LOGI(TAG, "stack HWM=%lu words", (unsigned long)uxTaskGetStackHighWaterMark(NULL));

    /* Базовый предохранитель от коррапта/мусора в path (видели '/spiffs/ ☺♠' в логе). */
    if (!path_is_printable_ascii(req->path)) {
        ESP_LOGE(TAG, "invalid path (non-ascii or empty) -> abort");
        player_cleanup(req, AUDIO_PLAYER_DONE_ERROR);
        return;
    }

    /* Включаем TX перед проигрыванием (идемпотентно). */
    tx_set_enabled_best_effort(true);

    FILE *f = fopen(req->path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen failed: %s (errno=%d)", req->path, errno);
        player_cleanup(req, AUDIO_PLAYER_DONE_ERROR);
        return;

    }

    /* Пишем в I2S блоками, с аккуратным handling TIMEOUT. */
    const TickType_t write_timeout = pdMS_TO_TICKS(1000);
    int consecutive_timeouts = 0;

    wav_info_t wi;
    const esp_err_t wav_err = wav_parse(f, &wi);

    if (wav_err == ESP_OK && wi.is_wav && wi.is_ima_adpcm) {
        /* ============================================================
         * WAV IMA ADPCM mono @ 16kHz
         * ============================================================ */
        ESP_LOGI(TAG, "WAV IMA ADPCM: block_align=%u spb=%u data=%u bytes",
                 (unsigned)wi.block_align,
                 (unsigned)wi.samples_per_block,
                 (unsigned)wi.data_size);

        /* temp buffers for ADPCM block decode */
        static uint8_t  s_blk[1024];
        static int16_t  s_blk_s16[2048];

        if (fseek(f, (long)wi.data_offset, SEEK_SET) != 0) {
            ESP_LOGE(TAG, "seek to data failed");
            aborted_error = true;
            goto done_file;
        }

        uint32_t remaining = wi.data_size;

        while (!s_stop && remaining >= wi.block_align) {
            const size_t to_read = wi.block_align;
            const size_t got = fread(s_blk, 1, to_read, f);
            if (got != to_read) {
                aborted_error = true;
                break;
            }
            remaining -= (uint32_t)got;

            const size_t ns = ima_adpcm_decode_block_mono(
                s_blk, got, s_blk_s16, (sizeof(s_blk_s16)/sizeof(s_blk_s16[0])),
                wi.samples_per_block);

            if (ns == 0) {
                ESP_LOGE(TAG, "IMA ADPCM decode failed");
                aborted_error = true;
                break;
            }

            /* stream decoded samples to I2S in chunks */
            size_t pos = 0;
            while (!s_stop && pos < ns) {
                const size_t n = (ns - pos > AUDIO_PLAYER_CHUNK_SAMPLES) ? AUDIO_PLAYER_CHUNK_SAMPLES : (ns - pos);

                for (size_t i = 0; i < n; i++) {
                    const int32_t w = s16_to_i2s_word(s_blk_s16[pos + i]);
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
                    if (consecutive_timeouts >= 3) {
                        ESP_LOGE(TAG, "too many TX timeouts -> abort playback");
                        s_stop = true;
                        aborted_error = true;
                        break;
                    }
                }

                pos += n;
            }
        }

        /* ignore trailing <block_align bytes */
    }
    else {
        /* ============================================================
         * RAW PCM s16 mono 16 kHz (legacy)
         * ============================================================ */
        if (wav_err == ESP_OK && wi.is_wav) {
            ESP_LOGW(TAG, "WAV detected but not supported (fmt not IMA ADPCM). Treating as ERROR.");
            aborted_error = true;
            goto done_file;
        }

        while (!s_stop) {
            const size_t n = fread(s_in_s16, sizeof(s_in_s16[0]), AUDIO_PLAYER_CHUNK_SAMPLES, f);
            if (n == 0) {
                break; /* EOF */
            }

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
                if (consecutive_timeouts >= 3) {
                    ESP_LOGE(TAG, "too many TX timeouts -> abort playback");
                    s_stop = true;
                    aborted_error = true;
                    break;
                }
            }
        }
    }

done_file:
    fclose(f);

    ESP_LOGI(TAG, "play done (stop=%d)", (int)s_stop);

    audio_player_done_reason_t reason = AUDIO_PLAYER_DONE_OK;
    if (aborted_error) {
        reason = AUDIO_PLAYER_DONE_ERROR;
    } else if (s_stop) {
        reason = AUDIO_PLAYER_DONE_STOPPED;
    }
    player_cleanup(req, reason);


}

void audio_player_register_done_cb(audio_player_done_cb_t cb, void *arg)
{
    /* Неблокирующая регистрация: это вызывается редко.
       Важно: cb не должен запускать новый play синхронно. */
    s_done_cb = cb;
    s_done_cb_arg = arg;
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
