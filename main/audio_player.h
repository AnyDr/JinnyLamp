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

// Причина завершения playback (для voice_fsm / логики anti-feedback).
typedef enum {
    AUDIO_PLAYER_DONE_OK = 0,        // дошли до EOF
    AUDIO_PLAYER_DONE_STOPPED,       // остановлено через audio_player_stop()
    AUDIO_PLAYER_DONE_ERROR,         // ошибка/аборт (invalid path, fopen fail, too many timeouts, etc.)
} audio_player_done_reason_t;

// Callback вызывается из контекста player_task перед удалением task.
// Важно: callback НЕ должен вызывать audio_player_play_*() напрямую (single-flight и static buffers).
typedef void (*audio_player_done_cb_t)(const char *path,
                                      audio_player_done_reason_t reason,
                                      void *arg);

// Зарегистрировать/снять callback завершения.
// cb == NULL => снять (arg игнорируется).
void audio_player_register_done_cb(audio_player_done_cb_t cb, void *arg);



#ifdef __cplusplus
}
#endif
