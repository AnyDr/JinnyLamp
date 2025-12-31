#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef struct __attribute__((packed)) {
    uint16_t seq;      // счётчик ответов
    int16_t  dv_mV;    // Vout_mV - Vzero_mV (signed)
} j_cur_raw4_t;

esp_err_t acs758_init(gpio_num_t gpio_adc);
esp_err_t acs758_zero_calibrate(void);
esp_err_t acs758_get_raw4(j_cur_raw4_t *out);
