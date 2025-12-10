#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "audio_i2s.h"
#include "led_control.h"
#include "asr_debug.h"

static const char *TAG = "JINNY_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Jinny lamp starting (modular I2S)...");

    ESP_ERROR_CHECK(led_control_init());
    ESP_ERROR_CHECK(audio_i2s_init());

    asr_debug_start();
}
