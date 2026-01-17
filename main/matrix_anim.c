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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "MATRIX_ANIM";

static TaskHandle_t s_task = NULL;
static volatile bool s_run = false;
static EventGroupHandle_t s_evt = NULL;
#define EVT_TASK_EXITED   (1U << 0)


static volatile bool s_paused = false;
static portMUX_TYPE  s_lock = portMUX_INITIALIZER_UNLOCKED;

/* Индекс “режима анимации” (исторически был нужен).
 * Сейчас эффекты идут через fx_engine, но next/prev оставляем живыми,
 * чтобы не ломать API и будущие сценарии.
 */
static volatile int  s_anim_idx = 0;


static void matrix_task(void *arg)
{
    (void)arg;

    #ifndef MATRIX_FPS
    #define MATRIX_FPS 25
    #endif

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000 / MATRIX_FPS);

        if (!s_run) {
            if (s_evt) {
                xEventGroupSetBits(s_evt, EVT_TASK_EXITED);
            }

            portENTER_CRITICAL(&s_lock);
            s_task = NULL;
            portEXIT_CRITICAL(&s_lock);

            vTaskDelete(NULL);
        }


    ESP_LOGI(TAG, "matrix task started (W=%u H=%u leds=%u)",
             (unsigned)MATRIX_W, (unsigned)MATRIX_H, (unsigned)MATRIX_LEDS_TOTAL);

    while (s_run) {

        vTaskDelayUntil(&last, period);

        bool paused;
        portENTER_CRITICAL(&s_lock);
        paused = s_paused;
        portEXIT_CRITICAL(&s_lock);

        if (!paused) {
            const uint32_t t_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            fx_engine_render(t_ms);

            const esp_err_t err = matrix_ws2812_show();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "matrix show failed: %s", esp_err_to_name(err));
            }
        }
        
    }


    if (s_evt) {
        xEventGroupSetBits(s_evt, EVT_TASK_EXITED);
    }

    portENTER_CRITICAL(&s_lock);
    s_task = NULL;
    portEXIT_CRITICAL(&s_lock);

    vTaskDelete(NULL);
}

esp_err_t matrix_anim_start(gpio_num_t data_gpio)
{
    portENTER_CRITICAL(&s_lock);
    const bool already = (s_task != NULL);
    portEXIT_CRITICAL(&s_lock);

    if (already) {
        // Уже запущено
        return ESP_OK;
    }

    if (!s_evt) {
        s_evt = xEventGroupCreate();
        if (!s_evt) {
            return ESP_ERR_NO_MEM;
        }
    }
    xEventGroupClearBits(s_evt, EVT_TASK_EXITED);


    // Инициализация WS2812 должна быть ДО show().
    // В логах ранее "WS2812 init ok" печатался именно из matrix_ws2812_init().
    esp_err_t err = matrix_ws2812_init(data_gpio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed (gpio=%d): %s", (int)data_gpio, esp_err_to_name(err));
        return err;
    }

    // 40% яркости (0.4 * 255 ≈ 102)
    matrix_ws2812_set_brightness(102);

    s_paused = false;
    s_run = true;



    // 4096 bytes stack, priority 5: достаточно для простого генератора кадра.
    BaseType_t ok = xTaskCreate(matrix_task, "matrix_anim", 4096, NULL, 5, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        s_run = false;
        return ESP_ERR_NO_MEM;
    }


    return ESP_OK;
}

void matrix_anim_stop(void)
{
    // Мягкий стоп: задача сама выйдет из цикла и удалится.
    s_run = false;
}

void matrix_anim_stop_and_wait(uint32_t timeout_ms)
{
    s_run = false;

    if (timeout_ms == 0) return;

    // Быстрый выход, если задачи уже нет
    portENTER_CRITICAL(&s_lock);
    const bool running = (s_task != NULL);
    portEXIT_CRITICAL(&s_lock);
    if (!running) return;

    if (s_evt) {
        (void)xEventGroupWaitBits(
            s_evt,
            EVT_TASK_EXITED,
            pdFALSE,   // не очищать бит автоматически
            pdTRUE,    // ждать все биты (тут один)
            pdMS_TO_TICKS(timeout_ms)
        );
        return;
    }

    // Fallback (на всякий): старое polling-ожидание
    const TickType_t t0 = xTaskGetTickCount();
    const TickType_t to = pdMS_TO_TICKS(timeout_ms);

    while (1) {
        portENTER_CRITICAL(&s_lock);
        const bool still = (s_task != NULL);
        portEXIT_CRITICAL(&s_lock);
        if (!still) break;

        vTaskDelay(pdMS_TO_TICKS(10));
        if ((xTaskGetTickCount() - t0) > to) break;
    }
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
