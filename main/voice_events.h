#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_EVT_BOOT_GREETING = 0,
    VOICE_EVT_THINKING,
    VOICE_EVT_CMD_OK,
    VOICE_EVT_POWER_OFF_GOODBYE,

    VOICE_EVT__COUNT
} voice_evt_t;

/* Инициализация маппинга/состояния (включая загрузку persistent masks из NVS). */
esp_err_t voice_events_init(void);

/* Запустить проигрывание фразы, привязанной к событию (async).
   Возвращает ESP_ERR_INVALID_STATE если плеер уже занят. */
esp_err_t voice_event_post(voice_evt_t evt);

#ifdef __cplusplus
}
#endif
