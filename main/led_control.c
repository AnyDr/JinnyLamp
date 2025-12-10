#include "led_control.h"
#include "driver/gpio.h"

#define LED_GPIO   21   // XIAO ESP32S3, active LOW

esp_err_t led_control_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // По умолчанию: LED выкл (active LOW => 1 = OFF)
    gpio_set_level(LED_GPIO, 1);

    return ESP_OK;
}

void led_control_set(bool on)
{
    // on=true => включаем (0), on=false => выключаем (1)
    gpio_set_level(LED_GPIO, on ? 0 : 1);
}
