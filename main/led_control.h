#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Инициализация GPIO под LED
esp_err_t led_control_init(void);

// Логический LED: true = включить, false = выключить.
// Реализация знает, что он active LOW.
void led_control_set(bool on);
