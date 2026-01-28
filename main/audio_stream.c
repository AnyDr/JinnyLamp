#include "audio_stream.h"

#include <string.h>
#include <limits.h>

#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"
#include "esp_err.h"

#include "audio_i2s.h"

static const char *TAG = "AUDIO_STREAM";

// Настройки ringbuffer
#define RB_ITEM_SIZE_BYTES    (AUDIO_STREAM_FRAME_SAMPLES * sizeof(int16_t))
#define RB_ITEMS              8   // 8 * 32ms ~= 256ms буфер
#define RB_SIZE_BYTES         (RB_ITEM_SIZE_BYTES * RB_ITEMS)

// Таймауты
#define I2S_READ_TIMEOUT_MS   500
#define RB_SEND_TIMEOUT_MS    0   // не блокируем producer (иначе backlog)

// Task
static TaskHandle_t      s_task = NULL;
static RingbufHandle_t   s_rb   = NULL;
static volatile uint32_t s_drop_frames = 0;

static inline int16_t clamp_s16(int32_t v)
{
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return (int16_t)v;
}

static void audio_stream_task(void *arg)
{
    (void)arg;

    // I2S frame: stereo int32 (L,R) * 512 => 1024 int32
    static int32_t i2s_frame[AUDIO_I2S_FRAME_SAMPLES];
    static int16_t mono_frame[AUDIO_STREAM_FRAME_SAMPLES];

    ESP_LOGI(TAG, "audio_stream_task started (mono s16 @ %d Hz, rb=%u bytes)",
             AUDIO_I2S_SAMPLE_RATE_HZ, (unsigned)RB_SIZE_BYTES);

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = audio_i2s_read(
            i2s_frame,
            sizeof(i2s_frame),
            &bytes_read,
            pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)
        );

        if (ret != ESP_OK || bytes_read == 0) {
            // Не рестартим систему: просто пропускаем и продолжаем.
            ESP_LOGW(TAG, "audio_i2s_read err=%s bytes=%u",
                     esp_err_to_name(ret), (unsigned)bytes_read);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        const size_t count = bytes_read / sizeof(int32_t);
        if (count < 2) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // Берём ЛЕВЫЙ канал: (L,R,L,R) => i2s_frame[i]
        // Ранее в asr_debug было: s24 = raw >> 8. Значит s16 примерно = raw >> 16.
        size_t out_n = 0;
        for (size_t i = 0; (i + 1) < count && out_n < AUDIO_STREAM_FRAME_SAMPLES; i += 2) {
            const int32_t raw = i2s_frame[i];
            const int32_t s16 = (raw >> 16); // mono s16
            mono_frame[out_n++] = clamp_s16(s16);
        }

        if (out_n != AUDIO_STREAM_FRAME_SAMPLES) {
            // На практике не ожидается, но не падаем.
            ESP_LOGW(TAG, "short frame: got %u samples", (unsigned)out_n);
            // добиваем нулями
            for (; out_n < AUDIO_STREAM_FRAME_SAMPLES; out_n++) mono_frame[out_n] = 0;
        }

        if (s_rb) {
            const BaseType_t ok = xRingbufferSend(s_rb, mono_frame, RB_ITEM_SIZE_BYTES,
                                                 pdMS_TO_TICKS(RB_SEND_TIMEOUT_MS));
            if (ok != pdTRUE) {
                // ringbuffer full -> drop frame
                s_drop_frames++;
            }
        }
    }
}

esp_err_t audio_stream_start(void)
{
    if (s_task && s_rb) {
        return ESP_OK;
    }

    s_drop_frames = 0;

    s_rb = xRingbufferCreate(RB_SIZE_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!s_rb) {
        ESP_LOGE(TAG, "xRingbufferCreate failed");
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t ok = xTaskCreate(audio_stream_task, "audio_stream", 4096, NULL, 6, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(audio_stream) failed");
        vRingbufferDelete(s_rb);
        s_rb = NULL;
        s_task = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void audio_stream_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_rb) {
        vRingbufferDelete(s_rb);
        s_rb = NULL;
    }
}

esp_err_t audio_stream_read_mono_s16(int16_t *dst,
                                    size_t   dst_samples,
                                    size_t  *out_samples_read,
                                    TickType_t timeout_ticks)
{
    if (out_samples_read) *out_samples_read = 0;

    if (!dst || dst_samples == 0) return ESP_ERR_INVALID_ARG;
    if (!s_rb) return ESP_ERR_INVALID_STATE;

    // Читаем максимум dst_samples, но ringbuffer выдаёт чанки.
    size_t total = 0;

    while (total < dst_samples) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
            s_rb,
            &item_size,
            timeout_ticks,
            (dst_samples - total) * sizeof(int16_t)
        );

        if (!item || item_size == 0) {
            // timeout / empty
            break;
        }

        const size_t n = item_size / sizeof(int16_t);
        memcpy(&dst[total], item, n * sizeof(int16_t));
        total += n;

        vRingbufferReturnItem(s_rb, item);

        // дальше уже не ждём бесконечно: если что-то пришло, вернёмся быстрее
        timeout_ticks = 0;
    }

    if (out_samples_read) *out_samples_read = total;

    return (total > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint32_t audio_stream_get_drop_frames(void)
{
    return s_drop_frames;
}
