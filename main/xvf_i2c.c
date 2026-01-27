#include "xvf_i2c.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "XVF_I2C";

static xvf_i2c_cfg_t s_cfg;
static bool s_inited = false;

static esp_err_t xvf_write_packet(uint8_t resid, uint8_t cmd, const uint8_t *payload, uint8_t payload_len)
{
    // Пакет: resid, cmd, payload_len, payload...
    uint8_t buf[3 + 32];
    if (payload_len > 32) return ESP_ERR_INVALID_SIZE;

    buf[0] = resid;
    buf[1] = cmd;
    buf[2] = payload_len;
    if (payload_len && payload) {
        memcpy(&buf[3], payload, payload_len);
    }

    return i2c_master_write_to_device(s_cfg.port, s_cfg.addr_7bit, buf, (size_t)(3 + payload_len), s_cfg.timeout_ticks);
}

static esp_err_t xvf_read_cmd(uint8_t resid, uint8_t cmd_read, uint8_t *out, size_t out_len)
{
    // Read request: resid, (cmd|0x80), (read_len + 1)   // +1 for status
    // Then read: status + payload
    uint8_t hdr[3];
    hdr[0] = resid;
    hdr[1] = (uint8_t)(cmd_read | 0x80);
    hdr[2] = (uint8_t)(out_len); // ожидаем уже “status + payload” длиной out_len

    esp_err_t err = i2c_master_write_to_device(s_cfg.port, s_cfg.addr_7bit, hdr, sizeof(hdr), s_cfg.timeout_ticks);
    if (err != ESP_OK) return err;

    return i2c_master_read_from_device(s_cfg.port, s_cfg.addr_7bit, out, out_len, s_cfg.timeout_ticks);
}

esp_err_t xvf_i2c_init(const xvf_i2c_cfg_t *cfg, gpio_num_t sda, gpio_num_t scl, uint32_t clk_hz)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    s_cfg = *cfg;

    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        // ВАЖНО: на плате обычно стоят внешние pull-up (2.2–4.7 кОм).
        // Внутренние pull-up слабые, но включить можно при необходимости.
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = clk_hz,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(s_cfg.port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(s_cfg.port, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        // Драйвер уже стоит (возможно, поднят другим модулем) — это ок.
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }


    s_inited = true;
    ESP_LOGI(TAG, "I2C ready: port=%d addr=0x%02X SDA=%d SCL=%d clk=%u Hz",
             (int)s_cfg.port, (unsigned)s_cfg.addr_7bit, (int)sda, (int)scl, (unsigned)clk_hz);

    return ESP_OK;
}

esp_err_t xvf_gpo_write(uint8_t pin, uint8_t level)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    const uint8_t payload[2] = { pin, (uint8_t)(level ? 1 : 0) };
    return xvf_write_packet(XVF_RESID_GPO, XVF_CMD_GPO_WRITE_VALUE, payload, sizeof(payload));
}

esp_err_t xvf_gpo_read_values(uint8_t values5[5], uint8_t *status)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!values5 || !status) return ESP_ERR_INVALID_ARG;

    // читаем: 1 status + 5 payload
    uint8_t buf[1 + 5] = {0};

    const esp_err_t err = xvf_read_cmd(XVF_RESID_GPO, XVF_CMD_GPO_READ_VALUES, buf, sizeof(buf));
    if (err != ESP_OK) return err;

    *status = buf[0];
    memcpy(values5, &buf[1], 5);
    return ESP_OK;
}

esp_err_t xvf_read_payload(uint8_t resid,
                           uint8_t cmd_read,
                           void *payload,
                           size_t payload_len,
                           uint8_t *status)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (!status) return ESP_ERR_INVALID_ARG;
    if (payload_len > 32) return ESP_ERR_INVALID_SIZE;
    if (payload_len && !payload) return ESP_ERR_INVALID_ARG;

    uint8_t buf[1 + 32] = {0}; // [status + payload]
    const size_t out_len = 1u + payload_len;

    const esp_err_t err = xvf_read_cmd(resid, cmd_read, buf, out_len);
    if (err != ESP_OK) return err;

    *status = buf[0];
    if (payload_len) memcpy(payload, &buf[1], payload_len);
    return ESP_OK;
}
