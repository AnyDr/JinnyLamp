#include "audio_tone_test.h"
#include "esp_heap_caps.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_i2s.h"

static const char *TAG = "AUDIO_TONE";

// ====== ВКЛ/ВЫКЛ (одна точка управления) ======
#define J_AUDIO_TONE_TEST_ENABLE          1   // <- поставь 0 чтобы полностью отключить
// ==============================================

// Параметры тона (можно крутить, не трогая остальной проект)
#define J_AUDIO_TONE_START_DELAY_MS       3000
#define J_AUDIO_TONE_FREQ_HZ              1000
#define J_AUDIO_TONE_DURATION_MS          1200
#define J_AUDIO_TONE_AMPLITUDE_PCT        8    // 1..30 обычно безопасно; 8% почти наверняка не клиппит

// В XVF/твоём RX используется схема "s24 в 32-bit слове", и из RX берёшь raw>>8.
// Для TX делаем то же: s24 << 8.
static inline int32_t s24_to_word32(int32_t s24)
{
    if (s24 >  0x007FFFFF) s24 =  0x007FFFFF;
    if (s24 < -0x00800000) s24 = -0x00800000;
    return (s24 << 8);
}

static TaskHandle_t s_task = NULL;

static void audio_tone_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(J_AUDIO_TONE_START_DELAY_MS));
        const size_t frame_words = (size_t)AUDIO_I2S_FRAME_SAMPLES;
    const size_t frame_bytes = frame_words * sizeof(int32_t);

    // Буфер под I2S TX: лучше DMA-capable, чтобы не было сюрпризов с драйвером
    int32_t *frame = (int32_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_DMA);
    if (!frame) {
        // fallback: хотя бы internal heap (если DMA занято)
        frame = (int32_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_INTERNAL);
    }
    if (!frame) {
        ESP_LOGE(TAG, "no mem for frame buffer (%u bytes)", (unsigned)frame_bytes);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "tone start: %d Hz, %d ms, amp=%d%% (s24<<8 -> I2S 32b stereo @ %d Hz)",
             J_AUDIO_TONE_FREQ_HZ,
             J_AUDIO_TONE_DURATION_MS,
             J_AUDIO_TONE_AMPLITUDE_PCT,
             AUDIO_I2S_SAMPLE_RATE_HZ);

    const float amp = (float)J_AUDIO_TONE_AMPLITUDE_PCT / 100.0f;
    const float step = 2.0f * (float)M_PI * (float)J_AUDIO_TONE_FREQ_HZ / (float)AUDIO_I2S_SAMPLE_RATE_HZ;

    

    float phase = 0.0f;
    const int total_samples = (AUDIO_I2S_SAMPLE_RATE_HZ * J_AUDIO_TONE_DURATION_MS) / 1000;
    int produced = 0;

    while (produced < total_samples) {
        const int chunk = (total_samples - produced) > AUDIO_I2S_FRAME_SAMPLES_PER_CH
                        ? AUDIO_I2S_FRAME_SAMPLES_PER_CH
                        : (total_samples - produced);

        // Заполняем chunk сэмплов (на канал), в буфере это 2*chunk int32 слов
        for (int i = 0; i < chunk; i++) {
            // full-scale s24 = +/- (2^23 - 1)
            const float s = sinf(phase) * amp;
            const int32_t s24 = (int32_t)(s * 8388607.0f); // 0x7FFFFF

            const int32_t w = s24_to_word32(s24);
            const int idx = i * 2;
            frame[idx + 0] = w; // L
            frame[idx + 1] = w; // R

            phase += step;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }

        size_t written = 0;
        const size_t bytes_to_write = (size_t)(chunk * 2) * sizeof(int32_t);

        const esp_err_t err = audio_i2s_write(frame, bytes_to_write, &written, pdMS_TO_TICKS(200));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "audio_i2s_write failed: %s (written=%u/%u)",
                     esp_err_to_name(err), (unsigned)written, (unsigned)bytes_to_write);
            break;
        }

        produced += chunk;
    }

    ESP_LOGI(TAG, "tone done");

    s_task = NULL;
    heap_caps_free(frame);
    vTaskDelete(NULL);
}

void audio_tone_test_start(void)
{
#if J_AUDIO_TONE_TEST_ENABLE
    if (s_task) {
        ESP_LOGW(TAG, "tone already running");
        return;
    }
    xTaskCreate(audio_tone_task, "audio_tone", 8192, NULL, 6, &s_task);

#else
    // disabled
    (void)TAG;
#endif
}
