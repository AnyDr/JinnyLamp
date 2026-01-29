#include "audio_i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


/*
 * audio_i2s.c
 *
 * Назначение:
 *   Инициализация I2S STD режима (ESP-IDF) в master-режиме.
 *   Создаём два канала:
 *     - TX (ESP -> XVF)
 *     - RX (XVF -> ESP)
 *
 * Примечание по данным:
 *   В текущей связке читаем int32_t слова, а в asr_debug.c берём s24 = raw >> 8.
 *   Это предполагает 24-битную полезную часть данных в 32-битном слове.
 */

#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s_std.h"

static const char *TAG = "AUDIO_I2S";

// -------- Твои реальные пины --------
#define I2S_BCK_PIN   8    // BCLK
#define I2S_WS_PIN    7    // LRCLK / WS
#define I2S_DO_PIN    44   // ESP -> XVF (DOUT)
#define I2S_DI_PIN    43   // XVF -> ESP (DIN)
// ------------------------------------

static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;
static bool s_tx_enabled = false;

esp_err_t audio_i2s_init(void)
{
    // Защита от повторной инициализации (типичный случай в отладке)
    if (tx_chan && rx_chan) {
        return ESP_OK;
    }

    esp_err_t err;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    err = i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        tx_chan = NULL;
        rx_chan = NULL;
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_DO_PIN,
            .din  = I2S_DI_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(tx) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(rx) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(tx) failed: %s", esp_err_to_name(err));
        return err;
    }
    s_tx_enabled = true;

    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(rx) failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2S master started at %d Hz (BCK=%d WS=%d DO=%d DI=%d)",
             AUDIO_I2S_SAMPLE_RATE_HZ, I2S_BCK_PIN, I2S_WS_PIN, I2S_DO_PIN, I2S_DI_PIN);
             
             /* TX always enabled: гарантируем, что DMA заполнен нулями на старте */
            (void)audio_i2s_tx_write_silence_ms(200, pdMS_TO_TICKS(1000));


    return ESP_OK;
}

esp_err_t audio_i2s_read(int32_t *buffer,
                         size_t   bytes_to_read,
                         size_t  *out_bytes_read,
                         TickType_t timeout_ticks)
{
    if (!rx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    const esp_err_t err = i2s_channel_read(
        rx_chan,
        buffer,
        bytes_to_read,
        &bytes_read,
        timeout_ticks
    );

    if (out_bytes_read) {
        *out_bytes_read = bytes_read;
    }

    return err;
}

esp_err_t audio_i2s_write(const int32_t *buffer,
                          size_t   bytes_to_write,
                          size_t  *out_bytes_written,
                          TickType_t timeout_ticks)
{
    if (!tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_written = 0;
    const esp_err_t err = i2s_channel_write(
        tx_chan,
        buffer,
        bytes_to_write,
        &bytes_written,
        timeout_ticks
    );

    if (out_bytes_written) {
        *out_bytes_written = bytes_written;
    }

    return err;
}

esp_err_t audio_i2s_tx_set_enabled(bool enabled)
{
    if (!tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Идемпотентность: не дергаем драйвер, если уже в нужном состоянии.
       Это убирает i2s_channel_enable(): already enabled. */
    if (enabled == s_tx_enabled) {
        return ESP_OK;
    }

    esp_err_t err = enabled ? i2s_channel_enable(tx_chan) : i2s_channel_disable(tx_chan);
    if (err == ESP_OK) {
        s_tx_enabled = enabled;
    }
    return err;
}


esp_err_t audio_i2s_tx_write_silence_ms(uint32_t ms, TickType_t timeout_ticks)
{
    if (!tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 256 стерео-фреймов (L+R) int32 = 256*2 words = 2048 bytes */
    static int32_t zeros[256 * 2] = {0};

    const uint32_t sr = (uint32_t)AUDIO_I2S_SAMPLE_RATE_HZ;
    uint32_t frames_total = (sr * ms) / 1000;
    if (frames_total == 0) {
        frames_total = 1;
    }

    while (frames_total > 0) {
        uint32_t frames_chunk = frames_total;
        if (frames_chunk > 256) {
            frames_chunk = 256;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = (size_t)frames_chunk * 2u * sizeof(int32_t);

        esp_err_t err = i2s_channel_write(tx_chan, zeros, bytes_to_write, &bytes_written, timeout_ticks);
        if (err != ESP_OK) {
            /* если даже тишину не можем протолкнуть — выходим (главное: дальше отключим TX в player) */
            return err;
        }

        frames_total -= frames_chunk;
    }

    return ESP_OK;
}
