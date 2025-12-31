#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "audio_i2s.h"
#include "led_control.h"
#include "asr_debug.h"
#include "matrix_anim.h"

#include "driver/gpio.h"

#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include "input_ttp223.h"
#include "sense_acs758.h"
#include "ctrl_bus.h"
#include "xvf_i2c.h"


// =======================
// GPIO mapping (hardware)
// =======================

// WS2812 DATA (после level-shifter 74AHCT125 -> матрицы)
#define MATRIX_DATA_GPIO         GPIO_NUM_3

// TTP223 touch output (momentary, active-HIGH). Управление кликами + sleep/wake.
#define TTP223_GPIO              GPIO_NUM_1

// ACS758 analog output -> ADC input (ток через лампу, high-side)
#define ACS758_GPIO              GPIO_NUM_4


// =======================
// XVF3800 I2C + GPO (MOSFET)
// =======================

// I2C к XVF3800 (служебная шина на плате XIAO<->XVF)
#define XVF_I2C_PORT             I2C_NUM_0
#define XVF_I2C_SDA_GPIO         GPIO_NUM_5
#define XVF_I2C_SCL_GPIO         GPIO_NUM_6
#define XVF_I2C_CLK_HZ           400000
#define XVF_I2C_TIMEOUT_MS       50

// XVF3800 GPO pin map (Seeed host-control): 11, 30, 31, 33, 39
// У нас MOSFET gate сидит на X0D11.
#define XVF_GPO_MOSFET_PIN       11

// Если вдруг по железу логика инвертирована: 1 = инвертировать
#define XVF_MOSFET_INVERT        0


// =======================
// TTP223 timing (ms)
// =======================

// Debounce (для polling-версии это время стабилизации уровня)
#define TTP_DEBOUNCE_MS          30

// Удержание для LONG-события (детектится в input_ttp223.c)
#define TTP_LONG_MS              3000

// Окно ожидания для набора double/triple кликов (после отпускания)
#define TTP_CLICK_GAP_MS         450

// После пробуждения (EXT0 по HIGH): удержание, чтобы считать это "включением"
#define TTP_WAKE_HOLD_MS         2000

// Задержка перед уходом в deep sleep после LONG (после отпускания)
#define TTP_SLEEP_DELAY_MS       3000


// =======================
// WS2812 power sequencing (ms)
// =======================

// После включения MOSFET пауза перед началом отправки данных.
#define MATRIX_PWR_ON_DELAY_MS   50

// После выключения MOSFET пауза (в основном для scope/гарантий).
#define MATRIX_PWR_OFF_DELAY_MS  30


static const char *TAG = "JINNY_MAIN";

// Предобъявление, чтобы sleep-task мог вызывать.
static void jinny_enter_deep_sleep(void);

// Sleep-task handle: чтобы не плодить несколько задач сна.
static TaskHandle_t s_sleep_task = NULL;


/* ============================================================
 * Deferred deep sleep task
 *
 * Зачем:
 *   - LONG подтверждаем в callback, но уход в сон делаем отдельной задачей.
 *   - Эта задача:
 *       1) ждёт отпускания кнопки (иначе получим мгновенный wake),
 *       2) ждёт TTP_SLEEP_DELAY_MS,
 *       3) вызывает jinny_enter_deep_sleep().
 * ============================================================ */
static void jinny_sleep_task(void *arg)
{
    (void)arg;

    // 1) Ждём отпускания кнопки (чтобы не словить немедленный wake)
    while (gpio_get_level(TTP223_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // 2) Отложенный сон (TTP_SLEEP_DELAY_MS)
    ESP_LOGI(TAG, "Deep sleep in %u ms", (unsigned)TTP_SLEEP_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(TTP_SLEEP_DELAY_MS));

    ESP_LOGI(TAG, "sleep task: calling enter_deep_sleep now");

    // 3) Реальный уход в сон
    jinny_enter_deep_sleep();

    // Обычно сюда не вернёмся, но на всякий случай:
    s_sleep_task = NULL;
    vTaskDelete(NULL);
}

static void jinny_schedule_deep_sleep(void)
{
    if (s_sleep_task) {
        // уже запланировано
        return;
    }

    ESP_LOGI(TAG, "Deep sleep scheduled in %u ms", (unsigned)TTP_SLEEP_DELAY_MS);

    if (xTaskCreate(jinny_sleep_task, "jinny_sleep", 4096, NULL, 5, &s_sleep_task) != pdPASS) {
        s_sleep_task = NULL;
        ESP_LOGE(TAG, "Failed to create sleep task");
        return;
    }

    ESP_LOGI(TAG, "sleep task created");
}


/* ============================================================
 * Power control helpers
 * ============================================================ */

// Управление MOSFET питания матриц через XVF3800 GPO.
static esp_err_t jinny_matrix_power_set(bool on)
{
    uint8_t level = on ? 1 : 0;
#if XVF_MOSFET_INVERT
    level = level ? 0 : 1;
#endif

    const esp_err_t err = xvf_gpo_write(XVF_GPO_MOSFET_PIN, level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "xvf_gpo_write(pin=%u, level=%u) failed: %s",
                 (unsigned)XVF_GPO_MOSFET_PIN, (unsigned)level, esp_err_to_name(err));
    }
    return err;
}

// Принудительно держим DATA WS2812 в LOW (чтобы не ловили мусор при питании OFF/переходах).
static void jinny_ws2812_data_force_low(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << MATRIX_DATA_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level(MATRIX_DATA_GPIO, 0);
}


/* ============================================================
 * Deep sleep entry
 * ============================================================ */
static void jinny_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep...");

    // 1) Остановить анимации
    matrix_anim_stop();
    vTaskDelay(pdMS_TO_TICKS(150));

    // 2) DATA=LOW
    jinny_ws2812_data_force_low();

    // 3) Выключить MOSFET питания матриц через XVF (X0D11)
    (void)jinny_matrix_power_set(false);
    vTaskDelay(pdMS_TO_TICKS(MATRIX_PWR_OFF_DELAY_MS));

    // 4) Настроить wake по touch (HIGH)
    if (!rtc_gpio_is_valid_gpio(TTP223_GPIO)) {
        ESP_LOGE(TAG, "TTP223_GPIO=%d is not RTC GPIO, cannot use EXT0 wake", (int)TTP223_GPIO);
    } else {
        ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(TTP223_GPIO, 1));
    }

    esp_deep_sleep_start();
}

static bool jinny_boot_wake_guard(void)
{
    // true => продолжаем обычный старт
    // false => сразу обратно в deep sleep
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        return true; // холодный старт или другой источник
    }

    // Для включения нужно удержание
    if (gpio_get_level(TTP223_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(TTP_WAKE_HOLD_MS));
        if (gpio_get_level(TTP223_GPIO) == 1) {
            ESP_LOGI(TAG, "Wake-hold OK, continue boot");
            return true;
        }
    }

    ESP_LOGI(TAG, "Wake tap too short, go back to sleep");
    return false;
}


/* ============================================================
 * TTP223 event callback
 *
 * Важно:
 *   - В текущей polling-версии input_ttp223.c callback вызывается из task-контекста.
 *   - SHORT/DOUBLE/TRIPLE мы rate-limit’им, чтобы не ловить “дубли”.
 *   - LONG подтверждаем дополнительным hold-check на GPIO (как анти-глюк TTP223).
 * ============================================================ */
static void ttp_evt_cb(ttp223_evt_t evt, void *user)
{
    (void)user;

    // Rate-limit только для SHORT/DOUBLE/TRIPLE
    static uint32_t s_last_action_ms = 0;
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    switch (evt) {
        case TTP223_EVT_SHORT: {
            if ((now_ms - s_last_action_ms) < 150) break;
            s_last_action_ms = now_ms;

            const ctrl_cmd_t c = { .type = CTRL_CMD_PAUSE_TOGGLE };
            (void)ctrl_bus_submit(&c);
            break;
        }

        case TTP223_EVT_DOUBLE: {
            if ((now_ms - s_last_action_ms) < 150) break;
            s_last_action_ms = now_ms;

            const ctrl_cmd_t c = { .type = CTRL_CMD_NEXT_EFFECT };
            (void)ctrl_bus_submit(&c);
            break;
        }

        case TTP223_EVT_TRIPLE: {
            if ((now_ms - s_last_action_ms) < 150) break;
            s_last_action_ms = now_ms;

            const ctrl_cmd_t c = { .type = CTRL_CMD_PREV_EFFECT };
            (void)ctrl_bus_submit(&c);
            break;
        }

        case TTP223_EVT_LONG: {
            ESP_LOGI(TAG, "APP LONG received (enter confirm)");

            // Защита от ложных LONG: подтверждаем, что удержание реальное.
            // Т.к. TTP223 "momentary" и может давать паразитные HIGH, перепроверяем.
            const uint32_t confirm_ms = 200;

            ESP_LOGI(TAG, "LONG detected, confirming hold %u ms...", (unsigned)confirm_ms);

            if (gpio_get_level(TTP223_GPIO) == 1) {
                vTaskDelay(pdMS_TO_TICKS(confirm_ms));
            }

            if (gpio_get_level(TTP223_GPIO) != 1) {
                ESP_LOGW(TAG, "LONG rejected (not held). Ignoring.");
                break;
            }

            // Уход в сон делаем отложенно отдельной задачей:
            //   - ждём отпускания кнопки
            //   - ждём TTP_SLEEP_DELAY_MS
            //   - выключаем матрицы и уходим в deep sleep
            jinny_schedule_deep_sleep();
            break;
        }

        default:
            break;
    }
}


/* ============================================================
 * app_main
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "Jinny lamp starting...");

    ESP_ERROR_CHECK(led_control_init());

    // XVF3800 control I2C (addr 0x2C).
    const xvf_i2c_cfg_t xvf_cfg = {
        .port = XVF_I2C_PORT,
        .addr_7bit = XVF_I2C_ADDR_DEFAULT,
        .timeout_ticks = pdMS_TO_TICKS(XVF_I2C_TIMEOUT_MS),
    };
    ESP_ERROR_CHECK(xvf_i2c_init(&xvf_cfg, XVF_I2C_SDA_GPIO, XVF_I2C_SCL_GPIO, XVF_I2C_CLK_HZ));

    // Кнопку поднимаем рано (нужна для wake guard и управления)
    const ttp223_cfg_t tcfg = {
        .gpio = TTP223_GPIO,
        .debounce_ms = TTP_DEBOUNCE_MS,
        .long_press_ms = TTP_LONG_MS,
        .click_gap_ms = TTP_CLICK_GAP_MS,
    };
    ESP_ERROR_CHECK(input_ttp223_init(&tcfg, ttp_evt_cb, NULL));

    // Control bus + FX engine
    ESP_ERROR_CHECK(ctrl_bus_init());

    // Если проснулись коротким тапом — обратно в сон
    if (!jinny_boot_wake_guard()) {
        jinny_enter_deep_sleep();
        return;
    }

    // Датчик тока
    ESP_ERROR_CHECK(acs758_init(ACS758_GPIO));

    // I2S поднимаем до asr_debug_start(), т.к. debug-задача читает RX сразу
    ESP_ERROR_CHECK(audio_i2s_init());

    // WS2812 инвариант: DATA=LOW -> power ON -> delay -> start sending.
    jinny_ws2812_data_force_low();
    ESP_ERROR_CHECK(jinny_matrix_power_set(true));
    vTaskDelay(pdMS_TO_TICKS(MATRIX_PWR_ON_DELAY_MS));

    ESP_LOGI(TAG, "Starting matrix ANIM on GPIO=%d", (int)MATRIX_DATA_GPIO);
    ESP_ERROR_CHECK(matrix_anim_start(MATRIX_DATA_GPIO));

    // Мониторинг аудио уровня
    asr_debug_start();

    ESP_LOGI(TAG, "System started");
}
