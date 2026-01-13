#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t j_wifi_start(void);

/* Возвращает true если STA подключён к AP и имеет канал AP */
bool      j_wifi_is_connected(void);

/* Текущий канал радио. Если не подключены, вернёт fallback-канал */
uint8_t   j_wifi_get_channel(void);
esp_err_t j_wifi_start_softap(const char *ssid, const char *pass, uint8_t channel);
esp_err_t j_wifi_stop_softap(void);
bool      j_wifi_is_softap_running(void);


esp_err_t j_wifi_start_softap(const char *ssid, const char *pass, uint8_t channel);
esp_err_t j_wifi_stop_softap(void);
bool      j_wifi_is_softap_running(void);


#ifdef __cplusplus
}
#endif
