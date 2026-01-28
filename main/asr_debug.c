#include "asr_debug.h"
#define ASR_DEBUG_LOOPBACK_ENABLE 0

/*
 * asr_debug.c
 *
 * Назначение:
 *   Лёгкая отладка входного аудио (Audio IN) без прямого владения I2S RX:
 *     1) Калибровка шума noise_floor на старте (по mono s16)
 *     2) Расчёт avg_abs и "level = % выше шума"
 *     3) LED-индикация с гистерезисом
 *     4) (опционально) loopback в I2S TX для проверки тракта
 *
 * Инвариант проекта:
 *   I2S RX читает только audio_stream (producer). asr_debug читает только из ringbuffer.
 */

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include "audio_i2s.h"      // нужен для AUDIO_I2S_SAMPLE_RATE_HZ (и для loopback, если включишь)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "audio_stream.h"
#include "led_control.h"

// ===== TEMP: I2S TX loopback (RX->TX) =====
// 0 = выкл (по умолчанию, чтобы не мешать плееру/тон-тесту и не ловить акустический фидбэк)
// 1 = вкл (только для отладки тракта)
#ifndef ASR_DEBUG_LOOPBACK_ENABLE
#define ASR_DEBUG_LOOPBACK_ENABLE 0
#endif
// =========================================


// ============================
// Настройки алгоритма/логов
// ============================

// Калибровка noise_floor: 50 фреймов * 512 samples ~= 1.6s при 16kHz
#define CAL_FRAMES                  50

// Таймаут чтения из ringbuffer (ms)
#define STREAM_READ_TIMEOUT_MS      500

// Сколько раз подряд можно получить timeout/ошибку до увеличенного backoff
// (не рестартим устройство, debug не должен валить систему)
#define MAX_CONSEC_TIMEOUTS         50

// Логировать уровень раз в N фреймов (0=выкл)
#define DEBUG_AUDIO_LEVEL           0
#if DEBUG_AUDIO_LEVEL
#define LOG_EVERY_N_FRAMES          10
#endif

// ===== I2S TX loopback (RX->TX) =====
// ВНИМАНИЕ: при подключённом усилителе/микрофонах легко получить акустический фидбэк (“вечный БИИИП”),
// а также конфликт с любым другим источником звука (tone test / будущий player).
// Поэтому по умолчанию OFF.
#ifndef ASR_DEBUG_LOOPBACK_ENABLE
#define ASR_DEBUG_LOOPBACK_ENABLE   0
#endif
// ===================================


// Гистерезис для LED по "level"
#define LEVEL_ON                    20
#define LEVEL_OFF                   10

// ============================

static const char *TAG = "ASR_DEBUG";

// mono frame: 512 samples int16
static int16_t mono[AUDIO_STREAM_FRAME_SAMPLES];


// noise_floor и last_level публикуем для других модулей (например DOA debug по Y)
static int32_t noise_floor = 0;
static volatile int32_t s_last_level = 0;


// ---------- helpers ----------

static inline int32_t iabs32(int32_t v) { return (v >= 0) ? v : -v; }

#if ASR_DEBUG_LOOPBACK_ENABLE
/*
 * Примечание по формату loopback:
 * - audio_stream отдаёт mono int16 (после конвертации из I2S RX).
 * - Для TX используем int32 stereo (L,R,L,R...).
 * - Укладываем mono в старшие 16 бит: raw = s16 << 16.
 *   Это согласуется с тем, что в ранних тестах s16 ~ raw >> 16.
 */
static void asr_debug_loopback_send(const int16_t *mono, size_t mono_samples)
{
    static int32_t tx_frame[AUDIO_I2S_FRAME_SAMPLES];

    // mono_samples ожидаем = 512. Делаем stereo int32: 1024 words.
    size_t out = 0;
    for (size_t i = 0; i < mono_samples && (out + 1) < AUDIO_I2S_FRAME_SAMPLES; i++) {
        const int32_t raw = ((int32_t)mono[i]) << 16;
        tx_frame[out++] = raw; // L
        tx_frame[out++] = raw; // R
    }

    const size_t bytes_to_write = out * sizeof(int32_t);
    size_t bytes_written = 0;

    const esp_err_t ret = audio_i2s_write(tx_frame,
                                         bytes_to_write,
                                         &bytes_written,
                                         pdMS_TO_TICKS(STREAM_READ_TIMEOUT_MS));

    if (ret != ESP_OK || bytes_written != bytes_to_write) {
        ESP_LOGW(TAG, "loopback write err=%s, written=%u of %u",
                 esp_err_to_name(ret),
                 (unsigned)bytes_written,
                 (unsigned)bytes_to_write);
    }
}
#endif


// ---------- core task ----------

static void audio_level_task(void *arg)
{
    (void)arg;

    esp_err_t ret;
    size_t samples_read = 0;
    int consec_timeouts = 0;

    ESP_LOGI(TAG, "audio_level_task started (input: mono s16 @ %d Hz)",
             AUDIO_I2S_SAMPLE_RATE_HZ);

    // ---------- ЭТАП 1: калибровка noise_floor ----------
    int64_t noise_sum = 0;
    int64_t noise_frames = 0;

    for (int f = 0; f < CAL_FRAMES; f++) {
        ret = audio_stream_read_mono_s16(mono,
                                         AUDIO_STREAM_FRAME_SAMPLES,
                                         &samples_read,
                                         pdMS_TO_TICKS(STREAM_READ_TIMEOUT_MS));

        if (ret != ESP_OK || samples_read == 0) {
            ESP_LOGW(TAG, "[CAL] stream read err=%s (samples=%u)",
                     esp_err_to_name(ret),
                     (unsigned)samples_read);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // avg_abs по фрейму
        int64_t sum_abs = 0;
        int32_t min_v = INT32_MAX;
        int32_t max_v = INT32_MIN;

        for (size_t i = 0; i < samples_read; i++) {
            const int32_t s = (int32_t)mono[i];
            if (s < min_v) min_v = s;
            if (s > max_v) max_v = s;
            sum_abs += iabs32(s);
        }

        const int32_t avg_abs = (samples_read > 0) ? (int32_t)(sum_abs / (int64_t)samples_read) : 0;

        noise_sum += avg_abs;
        noise_frames++;

#if DEBUG_AUDIO_LEVEL
        if ((f % 10) == 0) {
            ESP_LOGI(TAG, "[CAL] frame %d/%d min=%d max=%d avg_abs=%d",
                     f + 1, CAL_FRAMES, (int)min_v, (int)max_v, (int)avg_abs);
        }
#endif
    }

    if (noise_frames > 0) {
        noise_floor = (int32_t)(noise_sum / noise_frames);
        if (noise_floor < 1) noise_floor = 1; // предохранитель от деления на 0
        ESP_LOGI(TAG, "Noise calibrated: noise_floor=%d (from %lld frames)",
                 (int)noise_floor, (long long)noise_frames);
    } else {
        // Не рестартим устройство: debug просто работает “вслепую”
        noise_floor = 1;
        ESP_LOGE(TAG, "Noise calibration FAILED (no valid frames). Using noise_floor=1");
    }

    // ---------- ЭТАП 2: основной цикл ----------
    bool led_on = false;

#if DEBUG_AUDIO_LEVEL
    int frame_ctr = 0;
#endif

    while (1) {
        ret = audio_stream_read_mono_s16(mono,
                                         AUDIO_STREAM_FRAME_SAMPLES,
                                         &samples_read,
                                         pdMS_TO_TICKS(STREAM_READ_TIMEOUT_MS));

        if (ret != ESP_OK || samples_read == 0) {
            consec_timeouts++;
            if (consec_timeouts == MAX_CONSEC_TIMEOUTS) {
                ESP_LOGW(TAG, "Many consecutive stream timeouts (%d). Check audio_stream/I2S.",
                         consec_timeouts);
            }
            // backoff небольшой, чтобы не жечь CPU
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        consec_timeouts = 0;

        int64_t sum_abs = 0;
        int used = 0;

        for (size_t i = 0; i < samples_read; i++) {
            sum_abs += iabs32((int32_t)mono[i]);
            used++;
        }

        const int32_t avg_abs = (used > 0) ? (int32_t)(sum_abs / (int64_t)used) : 0;

        // level = "на сколько процентов громче шума"
        int32_t level = 0;
        if (noise_floor > 0 && avg_abs > noise_floor) {
            const int64_t num = (int64_t)avg_abs * 100;
            const int32_t ratio100 = (int32_t)(num / (int64_t)noise_floor);
            if (ratio100 > 100) {
                level = ratio100 - 100; // 0..∞ (% above noise)
            }
        }

        // publish for other modules (e.g. DOA debug)
        s_last_level = level;

        // LED hysteresis
        if (!led_on && level > LEVEL_ON) {
            led_on = true;
            led_control_set(true);
        } else if (led_on && level < LEVEL_OFF) {
            led_on = false;
            led_control_set(false);
        }

#if DEBUG_AUDIO_LEVEL
        if (((frame_ctr++) % LOG_EVERY_N_FRAMES) == 0) {
            float ratio_f = 0.0f;
            if (noise_floor > 0) ratio_f = (float)avg_abs / (float)noise_floor;
            int32_t delta = avg_abs - noise_floor;
            if (delta < 0) delta = 0;

            ESP_LOGI(TAG,
                     "used=%d avg_abs=%d noise=%d delta=%d ratio=%.2f level=%d led=%s drops=%u",
                     used, (int)avg_abs, (int)noise_floor, (int)delta, ratio_f, (int)level,
                     led_on ? "ON" : "OFF",
                     (unsigned)audio_stream_get_drop_frames());
        }
#endif

#if ASR_DEBUG_LOOPBACK_ENABLE
        asr_debug_loopback_send(mono, samples_read);
#endif

    }
}


// ---------- public API ----------

void asr_debug_start(void)
{
    const BaseType_t ok = xTaskCreate(
        audio_level_task,
        "audio_level_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(audio_level_task) failed");
        // Не делаем restart: приложение должно жить без debug-задачи.
    }
}

uint16_t asr_debug_get_level(void)
{
    int32_t v = s_last_level;
    if (v < 0) v = 0;
    if (v > 1000) v = 1000; // предохранитель
    return (uint16_t)v;
}
