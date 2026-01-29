#pragma once

/*
 * audio_i2s.h
 *
 * Назначение:
 *   Минимальный слой-обёртка над I2S STD драйвером ESP-IDF (i2s_std).
 *   Используется для связи ESP32-S3 <-> XVF3800:
 *     - RX: читаем аудио из XVF в формате 32-bit слова (фактически 24-bit данных)
 *     - TX: отправляем обратно (loopback), полезно для отладки канала
 *
 * Важно:
 *   В asr_debug.c предполагается, что 24-битные данные упакованы в 32-битное слово
 *   и извлекаются сдвигом >> 8.
 */

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// ===== Глобальные аудио-константы =====
#define AUDIO_I2S_SAMPLE_RATE_HZ           16000
#define AUDIO_I2S_FRAME_SAMPLES_PER_CH     512     // сэмплов на канал
#define AUDIO_I2S_CHANNELS                 2
#define AUDIO_I2S_FRAME_SAMPLES            (AUDIO_I2S_FRAME_SAMPLES_PER_CH * AUDIO_I2S_CHANNELS)
#define AUDIO_I2S_FRAME_BYTES              (AUDIO_I2S_FRAME_SAMPLES * sizeof(int32_t))
// ======================================

// Инициализация I2S (MASTER, 16 kHz, 32-bit, стерео)
esp_err_t audio_i2s_init(void);

// Обёртка над i2s_channel_read для RX-канала.
// bytes_to_read обычно = AUDIO_I2S_FRAME_BYTES.
esp_err_t audio_i2s_read(int32_t *buffer,
                         size_t   bytes_to_read,
                         size_t  *out_bytes_read,
                         TickType_t timeout_ticks);

// Обёртка над i2s_channel_write для TX-канала (loopback).
esp_err_t audio_i2s_write(const int32_t *buffer,
                          size_t   bytes_to_write,
                          size_t  *out_bytes_written,
                          TickType_t timeout_ticks);

/* NEW: чтобы гарантированно “заткнуть” бесконечный тон */
esp_err_t audio_i2s_tx_set_enabled(bool enabled);

/* NEW: записать тишину ms миллисекунд (стерео int32, 16 kHz) */
esp_err_t audio_i2s_tx_write_silence_ms(uint32_t ms, TickType_t timeout_ticks);