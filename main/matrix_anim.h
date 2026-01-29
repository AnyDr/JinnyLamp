#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start matrix animation task.
 *
 * Called on system start and on exit from SOFT OFF.
 * Creates animation task and initializes internal FX time.
 */
void matrix_anim_start(void);

/**
 * @brief Stop matrix animation task (fire-and-forget).
 *
 * Used for power transitions (SOFT OFF, deep sleep).
 * Does NOT block until task exit.
 */
void matrix_anim_stop(void);

/**
 * @brief Stop matrix animation task and wait for completion.
 *
 * Used when hardware must be safely powered down.
 */
void matrix_anim_stop_and_wait(void);

/**
 * @brief Toggle animation pause state.
 *
 * Pause freezes FX time but keeps:
 *  - task alive
 *  - WS2812 initialized
 *  - show() running
 *
 * Resume continues FX from the same state.
 */
void matrix_anim_pause_toggle(void);

/**
 * @brief Query pause state.
 */
bool matrix_anim_is_paused(void);

#ifdef __cplusplus
}
#endif
