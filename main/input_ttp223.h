#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef enum {
    TTP223_EVT_SHORT = 1,
    TTP223_EVT_DOUBLE,
    TTP223_EVT_TRIPLE,
    TTP223_EVT_LONG,
} ttp223_evt_t;

typedef void (*ttp223_evt_cb_t)(ttp223_evt_t evt, void *user);

typedef struct {
    gpio_num_t gpio;

    // Тайминги (мс)
    uint32_t debounce_ms;      // отсев дребезга (обычно 20..50)
    uint32_t long_press_ms;    // удержание для LONG (обычно 800..1500)
    uint32_t click_gap_ms;     // окно для набора кликов (обычно 250..450)
} ttp223_cfg_t;

esp_err_t input_ttp223_init(const ttp223_cfg_t *cfg, ttp223_evt_cb_t cb, void *user);
void      input_ttp223_deinit(void);
