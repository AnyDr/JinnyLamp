#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_SRC_REMOTE = 0,
    POWER_SRC_LOCAL_BTN,
    POWER_SRC_SERVER,
} power_src_t;

typedef enum {
    POWER_STATE_ON = 0,
    POWER_STATE_SOFT_OFF,
} power_state_t;

/**
 * Инициализация менеджера питания LED.
 * data_gpio   - GPIO линии DATA WS2812 (ESP32 side, до матриц)
 * mosfet_pin  - номер GPO XVF, который управляет MOSFET (например 11)
 * invert      - инверсия уровня на MOSFET (0/1)
 */
void power_mgmt_init(gpio_num_t data_gpio, uint8_t mosfet_pin, bool invert);

power_state_t power_mgmt_get_state(void);
power_src_t   power_mgmt_get_last_src(void);

/**
 * Держать DATA строго LOW (на случай остановленного RMT/драйвера).
 * Используется перед ON и перед OFF.
 */
void power_mgmt_ws2812_data_force_low(void);

/**
 * Низкоуровневый контроль MOSFET через XVF GPO.
 * Обычно используем _retry версию.
 */
esp_err_t power_mgmt_mosfet_set(bool on);
esp_err_t power_mgmt_mosfet_set_retry(bool on);

/**
 * Подготовка питания матриц (железный слой, без stop/join).
 * ON:  DATA=LOW -> MOSFET ON (retry) -> delay
 * OFF: DATA=LOW -> MOSFET OFF -> delay
 *
 * Эти функции НЕ меняют ctrl_bus и НЕ трогают DOA/ESPNOW.
 */
esp_err_t power_mgmt_led_power_on_prepare(void);
void      power_mgmt_led_power_off_prepare(void);

/**
 * Реальный SOFT OFF:
 *  - stop(join) anim
 *  - DATA=LOW
 *  - MOSFET OFF
 */
esp_err_t power_mgmt_enter_soft_off(power_src_t src);

/**
 * Выход из SOFT OFF:
 *  - DATA=LOW
 *  - MOSFET ON
 *  - delay
 *  - restart anim
 */
esp_err_t power_mgmt_exit_soft_off(power_src_t src);

#ifdef __cplusplus
}
#endif
