#include <stdint.h>
#include <stdbool.h>

#include "input_ttp223.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "TTP223";

/* ============================================================
 * input_ttp223.c (POLLING VERSION)
 *
 * Зачем:
 *   - Уходим от GPIO ISR + очереди + esp_timer.
 *   - У тебя было "queue full -> missed edges", что ломает клики/LONG.
 *   - Polling + стабильный debounce по времени не может "переполниться".
 *
 * Как работает:
 *   - Каждые POLL_MS читаем GPIO.
 *   - Делаем "стабильный уровень": изменение считается реальным,
 *     только если raw уровень держится debounce_ms.
 *   - На stable PRESS:
 *        стартуем отсчет удержания, сбрасываем long_sent.
 *   - Пока удерживается:
 *        при достижении long_press_ms генерим LONG (1 раз).
 *   - На stable RELEASE:
 *        если LONG не сработал -> инкремент кликов и запускаем окно click_gap_ms.
 *   - Когда окно click_gap_ms истекло и кнопка отпущена:
 *        генерим SHORT/DOUBLE/TRIPLE.
 *
 * Важно:
 *   - callback вызывается из task-контекста (не ISR, не esp_timer task).
 *   - Не делай в callback длительные блокировки (лучше "schedule task").
 * ============================================================ */

/* Частота опроса.
 * При CONFIG_FREERTOS_HZ=100 минимальный реальный шаг сна ~10 ms.
 * Поэтому держим POLL_MS = 10.
 */
#define TTP223_POLL_MS  10

static TaskHandle_t  s_task = NULL;

static ttp223_cfg_t     s_cfg;
static ttp223_evt_cb_t  s_cb = NULL;
static void            *s_user = NULL;

/* Сырые/стабильные уровни */
static uint8_t  s_raw_level = 0;
static uint8_t  s_stable_level = 0;
static uint32_t s_raw_change_ms = 0;

/* Состояние нажатия */
static uint32_t s_press_start_ms = 0;
static uint8_t  s_long_sent = 0;

/* Мультиклик */
static uint8_t  s_click_count = 0;
static uint32_t s_click_deadline_ms = 0;

/* Получение времени в мс (грубость зависит от tick rate) */
static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void emit_clicks_if_due(uint32_t t_ms)
{
    if (s_click_count == 0) return;

    /* Важно: клики фиксируем только когда кнопка отпущена */
    if (s_stable_level != 0) return;

    if (t_ms < s_click_deadline_ms) return;

    if (!s_cb) {
        s_click_count = 0;
        return;
    }

    const uint8_t n = s_click_count;
    s_click_count = 0;

    if (n == 1)       s_cb(TTP223_EVT_SHORT,  s_user);
    else if (n == 2)  s_cb(TTP223_EVT_DOUBLE, s_user);
    else              s_cb(TTP223_EVT_TRIPLE, s_user);
}

static void ttp223_task(void *arg)
{
    (void)arg;

    /* Инициализация уровней */
    s_raw_level = (uint8_t)(gpio_get_level(s_cfg.gpio) ? 1 : 0);
    s_stable_level = s_raw_level;
    s_raw_change_ms = now_ms();

    ESP_LOGI(TAG, "poll task start gpio=%d poll=%ums debounce=%ums long=%ums click_gap=%ums",
             (int)s_cfg.gpio,
             (unsigned)TTP223_POLL_MS,
             (unsigned)s_cfg.debounce_ms,
             (unsigned)s_cfg.long_press_ms,
             (unsigned)s_cfg.click_gap_ms);

    while (1) {
        const uint32_t t = now_ms();

        /* 1) Считываем raw */
        const uint8_t raw = (uint8_t)(gpio_get_level(s_cfg.gpio) ? 1 : 0);

        /* 2) Отслеживаем момент изменения raw */
        if (raw != s_raw_level) {
            s_raw_level = raw;
            s_raw_change_ms = t;
        }

        /* 3) Если raw держится debounce_ms, принимаем как стабильное изменение */
        if ((s_raw_level != s_stable_level) &&
            (t - s_raw_change_ms >= s_cfg.debounce_ms)) {

            s_stable_level = s_raw_level;

            if (s_stable_level) {
                /* -------- stable PRESS -------- */
                s_press_start_ms = t;
                s_long_sent = 0;

                /* ВАЖНО: если был набран 1 клик и началось новое нажатие,
                 * окно click_gap не должно "выстрелить" во время удержания.
                 * Поэтому просто оставляем счетчик, но дедлайн пересчитаем на release.
                 */
            } else {
                /* -------- stable RELEASE -------- */

                /* Если LONG уже был — клики не считаем */
                if (!s_long_sent) {
                    const uint32_t held = t - s_press_start_ms;

                    /* минимальная защита от "почти нуля" */
                    if (held >= s_cfg.debounce_ms) {
                        s_click_count++;

                        /* окно мультиклика стартуем от release */
                        s_click_deadline_ms = t + s_cfg.click_gap_ms;
                    }
                }

                /* Сброс long_sent при отпускании не делаем:
                 * он и так сбрасывается на следующем PRESS.
                 */
            }
        }

        /* 4) LONG: если удерживаем и ещё не отправляли */
        if (s_stable_level && !s_long_sent) {
            const uint32_t held = t - s_press_start_ms;
            if (held >= s_cfg.long_press_ms) {
                s_long_sent = 1;
                if (s_cb) s_cb(TTP223_EVT_LONG, s_user);

                /* После LONG клики игнорируем до отпускания */
                s_click_count = 0;
            }
        }

        /* 5) Если окно кликов истекло — эмитим SHORT/DOUBLE/TRIPLE */
        emit_clicks_if_due(t);

        vTaskDelay(pdMS_TO_TICKS(TTP223_POLL_MS));
    }
}

esp_err_t input_ttp223_init(const ttp223_cfg_t *cfg, ttp223_evt_cb_t cb, void *user)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_OK;

    s_cfg  = *cfg;
    s_cb   = cb;
    s_user = user;

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_cfg.gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // TTP223 active-HIGH, фиксируем LOW в покое
        .intr_type = GPIO_INTR_DISABLE,       // ВАЖНО: никаких ISR
    };

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    if (xTaskCreate(ttp223_task, "ttp223", 4096, NULL, 6, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init ok (polling)");
    return ESP_OK;
}

void input_ttp223_deinit(void)
{
    if (!s_task) return;

    vTaskDelete(s_task);
    s_task = NULL;

    /* Обнуляем коллбек/юзер, чтобы не было вызовов после deinit */
    s_cb = NULL;
    s_user = NULL;
}
