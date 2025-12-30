#include "matrix_anim.h"

/*
 * matrix_anim.c
 *
 * Реализация тестовой анимации "переливающийся диагональный градиент снизу-вверх".
 *
 * Модель работы:
 *   - задача matrix_task() с периодом ~10 FPS формирует кадр (перезаписывает все пиксели),
 *   - затем делает matrix_ws2812_show() ровно один раз на кадр.
 *
 * Инварианты:
 *   - Параллельные вызовы matrix_ws2812_show() из других модулей недопустимы
 *     (иначе будут гонки за буфер/тайминги и возможные артефакты).
 */

#include "matrix_ws2812.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "MATRIX_ANIM";

static TaskHandle_t s_task = NULL;
static volatile bool s_run = false;

/*
 * wheel()
 *
 * Преобразование 0..255 -> RGB "цветового круга" без float.
 * Никаких гамма-коррекций здесь нет: это именно быстрый тестовый генератор цвета.
 */
static void wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // классическая “радуга” без float
    pos = (uint8_t)(255u - pos);
    if (pos < 85u) {
        *r = (uint8_t)(255u - pos * 3u);
        *g = 0;
        *b = (uint8_t)(pos * 3u);
    } else if (pos < 170u) {
        pos = (uint8_t)(pos - 85u);
        *r = 0;
        *g = (uint8_t)(pos * 3u);
        *b = (uint8_t)(255u - pos * 3u);
    } else {
        pos = (uint8_t)(pos - 170u);
        *r = (uint8_t)(pos * 3u);
        *g = (uint8_t)(255u - pos * 3u);
        *b = 0;
    }
}

/*
 * anim_diag()
 *
 * Переливающийся диагональный градиент снизу-вверх:
 *   - базовый оттенок (base) двигается с кадром,
 *   - вклад X и инвертированного Y создаёт диагональ (низ -> верх).
 *
 * Важно:
 *   - Мы записываем ВСЕ пиксели кадра, поэтому предварительный clear() не нужен.
 *   - Переполнение uint8_t у hue намеренное (циклическая "обёртка" 0..255).
 */
static void anim_diag(uint32_t frame)
{
    const uint8_t base = (uint8_t)(frame & 0xFFu);

    // Коэффициенты наклона диагонали:
    // ky чуть больше kx, чтобы визуально ощущалось "снизу-вверх".
    const uint8_t kx = 4u;
    const uint8_t ky = 6u;

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        // y_inv: низ=0, верх=H-1
        const uint16_t y_inv = (uint16_t)(MATRIX_H - 1u - y);

        for (uint16_t x = 0; x < MATRIX_W; x++) {
            uint8_t r, g, b;

            // uint8_t overflow здесь ожидаем и используем как wrap-around 0..255
            const uint8_t hue = (uint8_t)(base +
                                          (uint8_t)(x * kx) +
                                          (uint8_t)(y_inv * ky));

            wheel(hue, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

static void matrix_task(void *arg)
{
    (void)arg;

    // ~10 FPS: достаточно, чтобы видеть движение, и не перегружать систему.
    const TickType_t period = pdMS_TO_TICKS(100);
    TickType_t last = xTaskGetTickCount();

    uint32_t frame = 0;
    s_run = true;

    ESP_LOGI(TAG, "matrix task started (W=%u H=%u leds=%u)",
             (unsigned)MATRIX_W, (unsigned)MATRIX_H, (unsigned)MATRIX_LEDS_TOTAL);

    while (s_run) {
        vTaskDelayUntil(&last, period);

        anim_diag(frame++);

        const esp_err_t err = matrix_ws2812_show();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "matrix show failed: %s", esp_err_to_name(err));
        }
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t matrix_anim_start(gpio_num_t data_gpio)
{
    if (s_task) {
        // Уже запущено
        return ESP_OK;
    }

    const esp_err_t err = matrix_ws2812_init(data_gpio);
    if (err != ESP_OK) {
        return err;
    }

    // 40% яркости (0.4 * 255 ≈ 102)
    matrix_ws2812_set_brightness(102);

    // 4096 bytes stack, priority 5: достаточно для простого генератора кадра.
    BaseType_t ok = xTaskCreate(matrix_task, "matrix_anim", 4096, NULL, 5, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void matrix_anim_stop(void)
{
    // Мягкий стоп: задача сама выйдет из цикла и удалится.
    s_run = false;
}
