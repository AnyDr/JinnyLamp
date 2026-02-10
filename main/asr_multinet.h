#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASR_CMD_NONE = 0,

    // Session control
    ASR_CMD_CANCEL_SESSION,
    ASR_CMD_SLEEP,
    ASR_CMD_OTA_ENTER,
    ASR_CMD_ASK_SERVER,

    // Playback / effects
    ASR_CMD_NEXT_EFFECT,
    ASR_CMD_PREV_EFFECT,
    ASR_CMD_PAUSE_TOGGLE,

    // Params
    ASR_CMD_BRIGHTNESS_UP,
    ASR_CMD_BRIGHTNESS_DOWN,
    ASR_CMD_SPEED_UP,
    ASR_CMD_SPEED_DOWN,
    ASR_CMD_VOLUME_UP,
    ASR_CMD_VOLUME_DOWN,
    ASR_CMD_MUTE,
} asr_cmd_t;

typedef struct {
    asr_cmd_t cmd;
    int       phrase_id;     // phrase_id from MultiNet (the id you used in esp_mn_commands_add)
    float     prob;          // probability (0..1)
    char      label[64];     // optional label (best-effort)
} asr_cmd_result_t;

typedef void (*asr_multinet_result_cb_t)(const asr_cmd_result_t *r, void *user_ctx);

/**
 * Init MultiNet engine:
 * - loads models from partition "model"
 * - creates internal task (idle until start_session)
 * - registers phrases via esp_mn_commands_*
 */
esp_err_t asr_multinet_init(asr_multinet_result_cb_t cb, void *user_ctx);
void      asr_multinet_deinit(void);

bool      asr_multinet_is_active(void);

/**
 * Start recognition session. Single-shot policy:
 * - when a command is DETECTED -> callback -> auto stop
 * - if MultiNet reports TIMEOUT -> callback(ASR_CMD_NONE,label="timeout") -> auto stop
 */
esp_err_t asr_multinet_start_session(uint32_t timeout_ms);
esp_err_t asr_multinet_stop_session(void);

#ifdef __cplusplus
}
#endif
