#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     ssid[32];
    char     pass[32];
    uint16_t port;       // обычно 80
    uint16_t timeout_s;  // авто-выход, напр. 300
} ota_portal_info_t;

esp_err_t ota_portal_start(const ota_portal_info_t *cfg);
void      ota_portal_stop(void);
bool      ota_portal_is_running(void);

#ifdef __cplusplus
}
#endif
