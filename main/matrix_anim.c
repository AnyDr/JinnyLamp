#include "matrix_anim.h"
#include "fx_engine.h"

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
#include "fx_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "MATRIX_ANIM";

static TaskHandle_t s_task = NULL;
static volatile bool s_run = false;

static volatile bool s_paused = false;
static volatile int  s_anim_idx = 0;
static portMUX_TYPE  s_lock = portMUX_INITIALIZER_UNLOCKED;


#if 0 // OLD VISUAL EFFECT (MOVE TO fx_effects_basic.c) - disable in matrix_anim// Вторая простая анимация: "бегущий огонёк" по всей ленте/матрицам.
static void anim_dot(uint32_t frame)
{
    // "Бегущий пиксель" по виртуальной матрице W×H.
    // Используем set_pixel_xy(), чтобы не зависеть от внутренней раскладки/змейки в драйвере.
    const uint32_t total = (uint32_t)MATRIX_W * (uint32_t)MATRIX_H;
    if (total == 0) {
        return;
    }

    const uint32_t pos = frame % total;
    const uint16_t x_on = (uint16_t)(pos % (uint32_t)MATRIX_W);
    const uint16_t y_on = (uint16_t)(pos / (uint32_t)MATRIX_W);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            if (x == x_on && y == y_on) {
                matrix_ws2812_set_pixel_xy(x, y, 255, 255, 255);
            } else {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
            }
        }
    }
}
#endif


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

        bool paused;
        int  anim;

        portENTER_CRITICAL(&s_lock);
        paused = s_paused;
        anim   = s_anim_idx;
        portEXIT_CRITICAL(&s_lock);

        if (paused) {
            continue; // держим последний кадр
        }

        if (anim == 0) {
            const uint32_t t_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            fx_engine_render(t_ms);

        } else {
            const uint32_t t_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            fx_engine_render(t_ms);
            frame++;

        }

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

void matrix_anim_pause_toggle(void)
{
    portENTER_CRITICAL(&s_lock);
    s_paused = !s_paused;
    portEXIT_CRITICAL(&s_lock);
}

void matrix_anim_next(void)
{
    portENTER_CRITICAL(&s_lock);
    s_anim_idx = (s_anim_idx + 1) % 2;
    s_paused = false;
    portEXIT_CRITICAL(&s_lock);
}

void matrix_anim_prev(void)
{
    portENTER_CRITICAL(&s_lock);
    s_anim_idx = (s_anim_idx + 2 - 1) % 2;
    s_paused = false;
    portEXIT_CRITICAL(&s_lock);
}
