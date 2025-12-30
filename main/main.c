#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "audio_i2s.h"
#include "led_control.h"
#include "asr_debug.h"
#include "matrix_anim.h"

#include "driver/gpio.h"

#define MATRIX_DATA_GPIO   GPIO_NUM_3   // GPIO DATA для WS2812 (через level shifter)

static const char *TAG = "JINNY_MAIN";

/*
 * main.c
 *
 * Назначение:
 *   Точка входа приложения (ESP-IDF).
 *   Поднимаем:
 *     1) status LED (простая индикация)
 *     2) I2S (ESP32-S3 <-> XVF3800), уже рабочая связь
 *     3) WS2812 анимация (RMT + DMA через led_strip)
 *     4) debug-задача аудио уровня (проверка входного аудио, loopback, LED по уровню)
 *
 * Инварианты:
 *   - matrix_anim_start() сам делает matrix_ws2812_init()
 *   - asr_debug_start() создаёт отдельную задачу и не блокирует app_main
 */

void app_main(void)
{
    ESP_LOGI(TAG, "Jinny lamp starting...");

    ESP_ERROR_CHECK(led_control_init());

    // ВАЖНО: I2S поднимаем до asr_debug_start(), т.к. debug-задача читает RX сразу
    ESP_ERROR_CHECK(audio_i2s_init());

    ESP_LOGI(TAG, "Starting matrix ANIM on GPIO=%d", (int)MATRIX_DATA_GPIO);
    ESP_ERROR_CHECK(matrix_anim_start(MATRIX_DATA_GPIO));

    // Стартуем задачу мониторинга аудио уровня + loopback
    asr_debug_start();

    ESP_LOGI(TAG, "System started");
}
