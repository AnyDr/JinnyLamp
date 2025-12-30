#include "led_control.h"
#include "driver/gpio.h"

/*
 * led_control.c
 *
 * Назначение:
 *   Управление встроенным LED на плате XIAO ESP32-S3.
 *   LED active LOW:
 *     - gpio=0 -> LED ON
 *     - gpio=1 -> LED OFF
 *
 * Замечание:
 *   Если у тебя другая ревизия платы/пин LED — меняется только LED_GPIO.
 */

#define LED_GPIO   GPIO_NUM_21   // XIAO ESP32-S3, active LOW

esp_err_t led_control_init(void)
{
    const gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    const esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    // По умолчанию: LED выкл (active LOW => 1 = OFF)
    (void)gpio_set_level(LED_GPIO, 1);

    return ESP_OK;
}

void led_control_set(bool on)
{
    // on=true => включаем (0), on=false => выключаем (1)
    (void)gpio_set_level(LED_GPIO, on ? 0 : 1);
}
