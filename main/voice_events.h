#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_EVT_BOOT_GREET = 0,
    VOICE_EVT_THINKING   = 1,
    VOICE_EVT_CMD_OK     = 2,
    VOICE_EVT_GOODBYE    = 3,
    VOICE_EVT_NO_CMD     = 4,
} voice_evt_t;


/* Инициализация маппинга/состояния (включая загрузку persistent masks из NVS). */
esp_err_t voice_events_init(void);

/* Запустить проигрывание фразы, привязанной к событию (async).
   Возвращает ESP_ERR_INVALID_STATE если плеер уже занят. */
esp_err_t voice_event_post(voice_evt_t evt);

#ifdef __cplusplus
}
#endif
