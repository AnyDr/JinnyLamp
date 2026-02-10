#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "audio_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_FSM_ST_IDLE = 0,
    VOICE_STATE_LISTENING_SESSION,
    VOICE_FSM_ST_SPEAKING,
    VOICE_FSM_ST_POST_GUARD,
} voice_fsm_state_t;

typedef struct {
    voice_fsm_state_t st;
    uint32_t          wake_seq;     // сколько wake событий принято
    uint32_t          speak_seq;    // сколько раз запускали speaking
    uint32_t          done_seq;     // сколько раз получили done callback
    audio_player_done_reason_t last_done_reason;
} voice_fsm_diag_t;

/* Запускает FSM и регистрирует audio_player done callback. */
esp_err_t voice_fsm_init(void);

/* Событие: wake detected (из WakeNet task). */
void voice_fsm_on_wake(void);

/* Диагностика (без блокировок на долго). */
void voice_fsm_get_diag(voice_fsm_diag_t *out);

#ifdef __cplusplus
}
#endif
