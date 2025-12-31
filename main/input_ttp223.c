#include <stdint.h>
#include <stdbool.h>

#include "input_ttp223.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include <esp_timer.h>
#include "esp_log.h"

static const char *TAG = "TTP223";

typedef struct {
    uint32_t tick_ms;
    uint8_t  level;
} edge_evt_t;

static QueueHandle_t s_q = NULL;
static TaskHandle_t  s_task = NULL;

static ttp223_cfg_t     s_cfg;
static ttp223_evt_cb_t  s_cb = NULL;
static void            *s_user = NULL;

static esp_timer_handle_t s_long_timer  = NULL;
static esp_timer_handle_t s_click_timer = NULL;

static volatile uint8_t  s_pressed    = 0;
static volatile uint8_t  s_long_fired = 0;

static uint32_t s_press_ms      = 0;
static uint32_t s_last_edge_ms  = 0;
static uint8_t  s_click_count   = 0;

static void long_timer_cb(void *arg)
{
    (void)arg;

    // Защита от "залипшего" s_pressed из-за пропущенного release-edge:
    // подтверждаем реальный уровень на пине.
    if (!s_pressed || s_long_fired) {
        return;
    }

    const int lvl = gpio_get_level(s_cfg.gpio);
    if (lvl != 1) {
        // RELEASE был (или стал) LOW, но edge могли отфильтровать дебаунсом/очередью.
        // Не генерим LONG.
        s_pressed = 0;
        return;
    }

    s_long_fired = 1;
    if (s_cb) s_cb(TTP223_EVT_LONG, s_user);
}


static void click_timer_cb(void *arg)
{
    (void)arg;

    const uint8_t n = s_click_count;
    s_click_count = 0;

    if (!s_cb) return;

    if (n == 1)       s_cb(TTP223_EVT_SHORT,  s_user);
    else if (n == 2)  s_cb(TTP223_EVT_DOUBLE, s_user);
    else if (n >= 3)  s_cb(TTP223_EVT_TRIPLE, s_user);
}

static void IRAM_ATTR gpio_isr(void *arg)
{
    const gpio_num_t gpio = (gpio_num_t)(int)(intptr_t)arg;

    const int lvl = gpio_get_level(gpio);

    edge_evt_t e = {
        .tick_ms = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS),
        .level   = (uint8_t)(lvl ? 1 : 0),
    };

    BaseType_t hp = pdFALSE;
    (void)xQueueSendFromISR(s_q, &e, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void input_task(void *arg)
{
    (void)arg;

    edge_evt_t e;
    while (1) {
        if (xQueueReceive(s_q, &e, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Debounce по времени между фронтами
        if ((e.tick_ms - s_last_edge_ms) < s_cfg.debounce_ms) {
            continue;
        }
        s_last_edge_ms = e.tick_ms;

        if (e.level) {
      // press (HIGH)
        s_pressed = 1;
        s_long_fired = 0;
        s_press_ms = e.tick_ms;

            // ВАЖНО: если идёт серия кликов, таймер "окна кликов" не должен сработать
            // во время удержания следующего нажатия, иначе получим ложный SHORT.
            if (s_click_timer) {
                (void)esp_timer_stop(s_click_timer);
            }

            if (s_long_timer) {
                (void)esp_timer_stop(s_long_timer);
                (void)esp_timer_start_once(s_long_timer,
                                   (uint64_t)s_cfg.long_press_ms * 1000ULL);
        }
        } else {
            // release (LOW)
            s_pressed = 0;

            if (s_long_timer) {
                (void)esp_timer_stop(s_long_timer);
            }

            // Если LONG уже сработал — клики не считаем
            if (s_long_fired) {
                continue;
            }

            const uint32_t held = e.tick_ms - s_press_ms;
            if (held < s_cfg.debounce_ms) {
                continue;
            }

            s_click_count++;

            if (s_click_timer) {
                (void)esp_timer_stop(s_click_timer);
                (void)esp_timer_start_once(s_click_timer,
                                           (uint64_t)s_cfg.click_gap_ms * 1000ULL);
            }
        }
    }
}

esp_err_t input_ttp223_init(const ttp223_cfg_t *cfg, ttp223_evt_cb_t cb, void *user)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_OK;

    s_cfg  = *cfg;
    s_cb   = cb;
    s_user = user;

    s_q = xQueueCreate(16, sizeof(edge_evt_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_cfg.gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // TTP223 active-HIGH, фиксируем LOW в покое
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    esp_timer_create_args_t t_long = {
        .callback = &long_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ttp_long",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&t_long, &s_long_timer);
    if (err != ESP_OK) return err;

    esp_timer_create_args_t t_click = {
        .callback = &click_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ttp_click",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&t_click, &s_click_timer);
    if (err != ESP_OK) return err;

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(s_cfg.gpio, gpio_isr, (void*)(intptr_t)s_cfg.gpio);
    if (err != ESP_OK) return err;

    if (xTaskCreate(input_task, "ttp223", 4096, NULL, 6, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init ok gpio=%d long=%ums click_gap=%ums",
             (int)s_cfg.gpio, (unsigned)s_cfg.long_press_ms, (unsigned)s_cfg.click_gap_ms);

    return ESP_OK;
}

void input_ttp223_deinit(void)
{
    if (!s_task) return;

    (void)gpio_isr_handler_remove(s_cfg.gpio);

    if (s_long_timer)  { (void)esp_timer_stop(s_long_timer);  (void)esp_timer_delete(s_long_timer);  s_long_timer = NULL; }
    if (s_click_timer) { (void)esp_timer_stop(s_click_timer); (void)esp_timer_delete(s_click_timer); s_click_timer = NULL; }

    vTaskDelete(s_task);
    s_task = NULL;

    if (s_q) { vQueueDelete(s_q); s_q = NULL; }
}
