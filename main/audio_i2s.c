#include "audio_i2s.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "AUDIO_I2S";

// -------- Твои реальные пины --------
#define I2S_BCK_PIN   8    // BCLK
#define I2S_WS_PIN    7    // LRCLK / WS
#define I2S_DO_PIN    44   // ESP -> XVF
#define I2S_DI_PIN    43   // XVF -> ESP
// ------------------------------------

static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;

esp_err_t audio_i2s_init(void)
{
    esp_err_t err;

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    err = i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
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

    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(rx) failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2S master started at %d Hz", AUDIO_I2S_SAMPLE_RATE_HZ);
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
    esp_err_t err = i2s_channel_read(
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
    esp_err_t err = i2s_channel_write(
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
