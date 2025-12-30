#include "asr_debug.h"

/*
 * asr_debug.c
 *
 * Назначение:
 *   Отладка аудио тракта ESP32-S3 <-> XVF3800:
 *     1) Калибровка шума noise_floor на старте
 *     2) Подсчёт avg_abs по левому каналу
 *     3) Уровень "на сколько % громче шума" (level)
 *     4) LED индикация с гистерезисом
 *     5) Loopback: пишем принятый фрейм обратно в TX
 *
 * Предположение о формате данных:
 *   samples[] читаются как int32_t. Полезная часть 24-bit.
 *   Извлечение: int32_t s24 = raw >> 8;
 *   Если формат/выравнивание другое — тут первым делом будет “космос” в значениях.
 */

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "audio_i2s.h"
#include "led_control.h"

// === Настройки алгоритма ===
#define CAL_FRAMES              50      // ~1.6s при 16 kHz и 512 samples/ch
#define I2S_READ_TIMEOUT_MS     500
#define I2S_MAX_CONSEC_ERRORS   50      // после этого — перезапуск
#define DEBUG_AUDIO_LEVEL       1       // 0 — минимум логов, 1 — логируем уровень

#if DEBUG_AUDIO_LEVEL
#define LOG_EVERY_N_FRAMES      10
#endif
// ============================

static const char *TAG = "ASR_DEBUG";

// Пороговые уровни в шкале "level" (проценты над шумом)
#define LEVEL_ON   20
#define LEVEL_OFF  10

static int32_t noise_floor = 0;

// Один frame: 512 сэмплов на канал, 2 канала => 1024 сэмпла int32_t
static int32_t samples[AUDIO_I2S_FRAME_SAMPLES];

_Static_assert(sizeof(samples) == AUDIO_I2S_FRAME_BYTES,
               "samples[] size must match one I2S frame");

static void audio_level_task(void *arg)
{
    (void)arg;

    esp_err_t ret;
    size_t bytes_read = 0;
    int error_count = 0;

    ESP_LOGI(TAG, "audio_level_task started");

    // ---------- ЭТАП 1: калибровка шума ----------
    int64_t noise_sum = 0;
    int64_t noise_count = 0;

    for (int f = 0; f < CAL_FRAMES; f++) {
        ret = audio_i2s_read(
            samples,
            sizeof(samples),
            &bytes_read,
            pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)
        );

        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "[CAL] audio_i2s_read error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        const size_t count = bytes_read / sizeof(int32_t);
        if (count < 2) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        int64_t sum_abs = 0;
        int used = 0;
        int32_t min_v = INT32_MAX;
        int32_t max_v = INT32_MIN;

        // Берём левый канал (L,R,L,R,...) => samples[i]
        for (size_t i = 0; i + 1 < count; i += 2) {
            const int32_t raw = samples[i];
            const int32_t s24 = raw >> 8;  // предполагаем 24-bit данные

            if (s24 < min_v) min_v = s24;
            if (s24 > max_v) max_v = s24;

            const int32_t abs_s = (s24 >= 0) ? s24 : -s24;
            sum_abs += abs_s;
            used++;
        }

        int32_t avg_abs = 0;
        if (used > 0) {
            avg_abs = (int32_t)(sum_abs / used);
        }

        noise_sum += avg_abs;
        noise_count++;

#if DEBUG_AUDIO_LEVEL
        if ((f % 10) == 0) {
            ESP_LOGI(TAG,
                     "[CAL] frame %d/%d, raw[0..5]=0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x, "
                     "min=%d max=%d avg_abs=%d",
                     f + 1, CAL_FRAMES,
                     (unsigned)samples[0], (unsigned)samples[1], (unsigned)samples[2],
                     (unsigned)samples[3], (unsigned)samples[4], (unsigned)samples[5],
                     (int)min_v, (int)max_v, (int)avg_abs);
        }
#endif
    }

    if (noise_count > 0) {
        noise_floor = (int32_t)(noise_sum / noise_count);
        ESP_LOGI(TAG, "Noise calibrated: noise_floor=%d (from %lld frames)",
                 (int)noise_floor, (long long)noise_count);
    } else {
        ESP_LOGE(TAG, "Noise calibration FAILED (no valid frames). Restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    // ---------- ЭТАП 2: основной цикл ----------
    bool led_on = false;

#if DEBUG_AUDIO_LEVEL
    int frame_ctr = 0;
#endif

    while (1) {
        ret = audio_i2s_read(
            samples,
            sizeof(samples),
            &bytes_read,
            pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)
        );

        if (ret != ESP_OK || bytes_read == 0) {
            if (++error_count > I2S_MAX_CONSEC_ERRORS) {
                ESP_LOGE(TAG, "Too many I2S errors in a row, restarting...");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }

            ESP_LOGW(TAG, "audio_i2s_read error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        error_count = 0;

        const size_t count = bytes_read / sizeof(int32_t);
        if (count < 2) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        int64_t sum_abs = 0;
        int used = 0;
        int32_t min_v = INT32_MAX;
        int32_t max_v = INT32_MIN;

        for (size_t i = 0; i + 1 < count; i += 2) {
            const int32_t raw = samples[i];
            const int32_t s24 = raw >> 8;

            if (s24 < min_v) min_v = s24;
            if (s24 > max_v) max_v = s24;

            const int32_t abs_s = (s24 >= 0) ? s24 : -s24;
            sum_abs += abs_s;
            used++;
        }

        int32_t avg_abs = 0;
        if (used > 0) {
            avg_abs = (int32_t)(sum_abs / used);
        }

        // level = "на сколько процентов громче шума"
        int32_t level = 0;
        if (noise_floor > 0 && avg_abs > noise_floor) {
            const int64_t num = (int64_t)avg_abs * 100;
            const int32_t ratio100 = (int32_t)(num / noise_floor);  // x100
            if (ratio100 > 100) {
                level = ratio100 - 100;
            }
        }

        // Гистерезис для LED
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
            if (noise_floor > 0) {
                ratio_f = (float)avg_abs / (float)noise_floor;
            }

            int32_t delta = avg_abs - noise_floor;
            if (delta < 0) delta = 0;

            ESP_LOGI(TAG,
                     "samples=%d used=%d avg_abs=%d noise=%d delta=%d ratio=%.2f level=%d led=%s",
                     (int)count, used, (int)avg_abs, (int)noise_floor, (int)delta, ratio_f, (int)level,
                     led_on ? "ON" : "OFF");
        }
#endif

        // Loopback обратно в XVF (для проверки TX тракта)
        size_t bytes_written = 0;
        ret = audio_i2s_write(
            samples,
            bytes_read,
            &bytes_written,
            pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)
        );

        if (ret != ESP_OK || bytes_written != bytes_read) {
            ESP_LOGW(TAG, "audio_i2s_write err=%s, written=%d of %d",
                     esp_err_to_name(ret), (int)bytes_written, (int)bytes_read);
        }
    }
}

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
        // Тут намеренно не делаем esp_restart(): пусть приложение продолжит жить без debug-задачи.
    }
}
