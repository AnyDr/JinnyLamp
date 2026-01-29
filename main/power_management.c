#include "power_management.h"
#include "matrix_ws2812.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "matrix_anim.h"
#include "xvf_i2c.h"  // xvf_gpo_write()

// =======================
// Tunables (keep old behaviour)
// =======================

// После включения MOSFET пауза перед началом отправки данных
#define PM_PWR_ON_DELAY_MS    50

// После выключения MOSFET пауза (для гарантии/осциллографа)
#define PM_PWR_OFF_DELAY_MS   30

// Retry при включении питания матрицы (XVF GPO может быть не готов сразу после flash/boot)
#define PM_PWR_ON_RETRIES     5
#define PM_PWR_ON_RETRY_MS    50

// Таймаут на stop/join anim при входе в SOFT OFF
#define PM_ANIM_STOP_TIMEOUT_MS 2000

static const char *TAG = "POWER_MGMT";

static gpio_num_t        s_data_gpio  = GPIO_NUM_NC;
static uint8_t           s_mosfet_pin = 0;
static bool              s_invert     = false;

static power_state_t     s_state      = POWER_STATE_ON;
static power_src_t       s_last_src   = POWER_SRC_REMOTE;

static SemaphoreHandle_t s_pm_lock    = NULL;

static inline bool pm_is_inited(void)
{
    return (s_data_gpio != GPIO_NUM_NC) && (s_pm_lock != NULL);
}

void power_mgmt_init(gpio_num_t data_gpio, uint8_t mosfet_pin, bool invert)
{
    s_data_gpio  = data_gpio;
    s_mosfet_pin = mosfet_pin;
    s_invert     = invert;

    if (!s_pm_lock) {
        s_pm_lock = xSemaphoreCreateMutex();
    }

    // По умолчанию после boot считаем "ON" (как и было: пытаемся включить матрицу)
    s_state    = POWER_STATE_ON;
    s_last_src = POWER_SRC_REMOTE;

    ESP_LOGI(TAG, "init: data_gpio=%d mosfet_pin=%u invert=%u",
             (int)s_data_gpio, (unsigned)s_mosfet_pin, (unsigned)s_invert);
}

power_state_t power_mgmt_get_state(void)
{
    return s_state;
}

power_src_t power_mgmt_get_last_src(void)
{
    return s_last_src;
}

void power_mgmt_ws2812_data_force_low(void)
{
    if (s_data_gpio == GPIO_NUM_NC) return;

    // На случай, если RMT/драйвер уже остановлен: держим DATA в нуле.
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_data_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level(s_data_gpio, 0);
}

esp_err_t power_mgmt_mosfet_set(bool on)
{
    uint8_t level = on ? 1 : 0;
    if (s_invert) {
        level = level ? 0 : 1;
    }

    const esp_err_t err = xvf_gpo_write(s_mosfet_pin, level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "xvf_gpo_write(pin=%u, level=%u) failed: %s",
                 (unsigned)s_mosfet_pin, (unsigned)level, esp_err_to_name(err));
    }
    return err;
}

esp_err_t power_mgmt_mosfet_set_retry(bool on)
{
    esp_err_t err = ESP_FAIL;

    for (uint32_t i = 0; i < PM_PWR_ON_RETRIES; i++) {
        err = power_mgmt_mosfet_set(on);
        if (err == ESP_OK) return ESP_OK;

        ESP_LOGW(TAG, "MOSFET %s retry %u/%u in %u ms (last=%s)",
                 on ? "ON" : "OFF",
                 (unsigned)(i + 1), (unsigned)PM_PWR_ON_RETRIES,
                 (unsigned)PM_PWR_ON_RETRY_MS, esp_err_to_name(err));

        vTaskDelay(pdMS_TO_TICKS(PM_PWR_ON_RETRY_MS));
    }

    return err;
}

esp_err_t power_mgmt_led_power_on_prepare(void)
{
    // WS2812 power sequencing:
    // DATA=LOW -> power ON -> delay -> start sending.
    power_mgmt_ws2812_data_force_low();

    const esp_err_t pwr_err = power_mgmt_mosfet_set_retry(true);
    if (pwr_err != ESP_OK) {
        ESP_LOGE(TAG, "Matrix power ON failed after retries: %s. LED matrix will stay OFF.",
                 esp_err_to_name(pwr_err));
        // Инвариант WS2812 (важно): если питание не включили, НЕ стартуем show.
        // DATA уже удерживается LOW.
        return pwr_err;
    }

    vTaskDelay(pdMS_TO_TICKS(PM_PWR_ON_DELAY_MS));
    return ESP_OK;
}

void power_mgmt_led_power_off_prepare(void)
{
    // stop/join делается выше уровнем (soft off / deep sleep).
    // Здесь только "железо".
    power_mgmt_ws2812_data_force_low();
    (void)power_mgmt_mosfet_set(false);
    vTaskDelay(pdMS_TO_TICKS(PM_PWR_OFF_DELAY_MS));
}

esp_err_t power_mgmt_enter_soft_off(power_src_t src)
{
    if (!pm_is_inited()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_pm_lock, portMAX_DELAY);

    if (s_state == POWER_STATE_SOFT_OFF) {
        ESP_LOGI(TAG, "SOFT OFF already active (src=%d)", (int)src);
        s_last_src = src;
        xSemaphoreGive(s_pm_lock);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Entering SOFT OFF (src=%d)", (int)src);

    // 1) Stop anim task and wait (join)
    matrix_anim_stop_and_wait(pdMS_TO_TICKS(PM_ANIM_STOP_TIMEOUT_MS));

    // Важно: сбросить led_strip/RMT, чтобы следующий start сделал полноценный init GPIO/RMT
    matrix_ws2812_deinit();


    // 2) Hardware power-off sequence
    power_mgmt_led_power_off_prepare();

    s_state    = POWER_STATE_SOFT_OFF;
    s_last_src = src;

    ESP_LOGI(TAG, "SOFT OFF entered");

    xSemaphoreGive(s_pm_lock);
    return ESP_OK;
}

esp_err_t power_mgmt_exit_soft_off(power_src_t src)
{
    if (!pm_is_inited()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_pm_lock, portMAX_DELAY);

    if (s_state == POWER_STATE_ON) {
        ESP_LOGI(TAG, "Already ON (src=%d)", (int)src);
        s_last_src = src;
        xSemaphoreGive(s_pm_lock);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Exiting SOFT OFF (src=%d)", (int)src);

    // 1) Power-on prepare (DATA low -> MOSFET ON -> delay)
    const esp_err_t err = power_mgmt_led_power_on_prepare();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power ON matrix, stay in SOFT OFF");
        xSemaphoreGive(s_pm_lock);
        return err;
    }

    // 2) Restart anim task
    ESP_ERROR_CHECK(matrix_anim_start(s_data_gpio));

    s_state    = POWER_STATE_ON;
    s_last_src = src;

    ESP_LOGI(TAG, "SOFT OFF exited, system ON");

    xSemaphoreGive(s_pm_lock);
    return ESP_OK;
}
