#pragma once

/*
 * xvf_i2c.h
 *
 * Минимальный “host-control” по I2C для XVF3800:
 * - формирование пакетов в стиле Seeed host examples (resid/cmd/len/payload)
 * - чтение/запись GPO
 *
 * Источник протокола: Seeed wiki “Controlling XVF3800 GPIO via XIAO ESP32-S3”.
 */

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XVF_I2C_ADDR_DEFAULT      0x2C  // 7-bit

// XVF “servicer” IDs / commands (Seeed examples)
#define XVF_RESID_GPO             20
#define XVF_CMD_GPO_READ_VALUES   0
#define XVF_CMD_GPO_WRITE_VALUE   1

typedef struct {
    i2c_port_t  port;           // обычно I2C_NUM_0
    uint8_t     addr_7bit;       // обычно 0x2C
    TickType_t  timeout_ticks;   // например pdMS_TO_TICKS(50)
} xvf_i2c_cfg_t;

// Инициализация I2C-драйвера (если уже установлен — не падаем).
esp_err_t xvf_i2c_init(const xvf_i2c_cfg_t *cfg,
                       gpio_num_t sda,
                       gpio_num_t scl,
                       uint32_t clk_hz);

// Записать логический уровень на XVF GPO pin (pin = 11/30/31/33/39, level=0/1).
esp_err_t xvf_gpo_write(uint8_t pin, uint8_t level);

// Прочитать 5 байт GPO значений: [X0D11, X0D30, X0D31, X0D33, X0D39], + status.
esp_err_t xvf_gpo_read_values(uint8_t values5[5], uint8_t *status);

#ifdef __cplusplus
}
#endif

// Generic read: returns status byte + payload bytes.
// payload_len must be <= 32 (same limit as write packet buffer philosophy).
esp_err_t xvf_read_payload(uint8_t resid,
                           uint8_t cmd_read,
                           void *payload,
                           size_t payload_len,
                           uint8_t *status);
