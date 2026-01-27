#pragma once

#include <stdint.h>
/*
 * asr_debug.h
 *
 * Назначение:
 *   Отладочная задача, которая:
 *     - калибрует шумовой порог (noise_floor) по входному аудио (RX)
 *     - считает уровень сигнала относительно шума
 *     - включает/выключает status LED по гистерезису
 *     - делает loopback: пишет прочитанное обратно в TX (проверка полного тракта)
 */

void     asr_debug_start(void);

/* level = "на сколько процентов громче шума" (см. asr_debug.c), 0..N */
uint16_t asr_debug_get_level(void);
