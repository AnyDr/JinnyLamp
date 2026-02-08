#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Voice Events — v1 dictionary
 *
 * Группы:
 *  A) Lifecycle / Power
 *  B) Session / Wake
 *  C) Command outcomes
 *  D) Server / LLM flow
 *  E) OTA lifecycle
 *  F) Errors / Diagnostics
 *
 * Последний элемент ОБЯЗАТЕЛЬНО VOICE_EVT__COUNT.
 * ============================================================ */

typedef enum {

    /* ---------- A) Lifecycle / Power ---------- */

    VOICE_EVT_BOOT_HELLO = 0,       /* cold boot */
    VOICE_EVT_DEEP_WAKE_HELLO,      /* wake from deep sleep */
    VOICE_EVT_DEEP_SLEEP_BYE,       /* enter deep sleep */
    VOICE_EVT_SOFT_ON_HELLO,        /* exit SOFT OFF */
    VOICE_EVT_SOFT_OFF_BYE,         /* enter SOFT OFF */


    /* ---------- B) Session / Wake ---------- */

    VOICE_EVT_WAKE_DETECTED,
    VOICE_EVT_SESSION_CANCELLED,
    VOICE_EVT_NO_CMD_TIMEOUT,
    VOICE_EVT_BUSY_ALREADY_LISTENING,


    /* ---------- C) Command outcomes ---------- */

    VOICE_EVT_CMD_OK,
    VOICE_EVT_CMD_FAIL,
    VOICE_EVT_CMD_UNSUPPORTED,


    /* ---------- D) Server / LLM flow ---------- */

    VOICE_EVT_NEED_THINKING_SERVER,
    VOICE_EVT_SERVER_UNAVAILABLE,
    VOICE_EVT_SERVER_TIMEOUT,
    VOICE_EVT_SERVER_ERROR,


    /* ---------- E) OTA lifecycle ---------- */

    VOICE_EVT_OTA_ENTER,
    VOICE_EVT_OTA_OK,
    VOICE_EVT_OTA_FAIL,
    VOICE_EVT_OTA_TIMEOUT,


    /* ---------- F) Errors / Diagnostics ---------- */

    VOICE_EVT_ERR_GENERIC,
    VOICE_EVT_ERR_STORAGE,
    VOICE_EVT_ERR_AUDIO,


    /* ---------- sentinel ---------- */

    VOICE_EVT__COUNT

} voice_evt_t;


/* Инициализация voice events:
 * - сброс RAM shuffle-масок
 * - чтение persistent масок из NVS (lifecycle события) */
esp_err_t voice_events_init(void);


/* Проиграть фразу события (async).
 * Политика v1:
 *   - если плеер занят → ошибка (no-interrupt policy)
 *   - формат: PCM s16 mono 16k */
esp_err_t voice_event_post(voice_evt_t evt);


#ifdef __cplusplus
}
#endif
