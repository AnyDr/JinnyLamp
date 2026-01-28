#pragma once

/*
 * audio_stream.h
 *
 * Назначение:
 *   Единая точка захвата аудио с I2S RX (XVF -> ESP).
 *   Один producer-task читает stereo int32 из audio_i2s_read(),
 *   конвертирует в mono int16 (16 kHz) и пишет в ringbuffer.
 *
 * Потребители (asr_debug / WakeNet / VAD / др.) читают через
 * audio_stream_read_mono_s16().
 *
 * Инвариант: НИКАКИХ прямых audio_i2s_read() вне audio_stream.c
 */

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// mono frame: 512 samples @16kHz => ~32 ms
#define AUDIO_STREAM_FRAME_SAMPLES   512

esp_err_t audio_stream_start(void);
void      audio_stream_stop(void);

/*
 * Чтение mono s16.
 * - dst: буфер на min(dst_samples, AUDIO_STREAM_FRAME_SAMPLES) или больше.
 * - out_samples_read: сколько реально прочитали (может быть 0 при timeout).
 */
esp_err_t audio_stream_read_mono_s16(int16_t *dst,
                                    size_t   dst_samples,
                                    size_t  *out_samples_read,
                                    TickType_t timeout_ticks);

// Для диагностики/статистики (не критично для работы)
uint32_t audio_stream_get_drop_frames(void);

#ifdef __cplusplus
}
#endif
