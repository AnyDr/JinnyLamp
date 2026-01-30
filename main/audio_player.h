#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация (включая mutex/состояние)
esp_err_t audio_player_init(void);

// Асинхронно проиграть PCM s16le mono 16kHz из файла.
// Файл читается чанками, конвертится в s24<<8 stereo int32 и уходит в audio_i2s_write().
esp_err_t audio_player_play_pcm_s16_mono_16k(const char *path);

// Остановить текущее воспроизведение (мягко).
void audio_player_stop(void);

// Volume 0..100 (software gain). Default 100 unless set by audio_bus/NVS.
void     audio_player_set_volume_pct(uint8_t vol_pct);
uint8_t  audio_player_get_volume_pct(void);


#ifdef __cplusplus
}
#endif
