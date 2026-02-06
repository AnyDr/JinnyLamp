#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

bool wake_wakenet_detect(const int16_t *pcm, int samples);


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *wn_model_name;   // строка имени модели, найденной в "model" partition
    int         sample_rate_hz;  // что говорит wakenet->get_samp_rate()
} wake_wakenet_info_t;

/* Инициализация WakeNet (поиск модели в partition "model", create handle).
   Task пока НЕ запускаем (это будет следующий шаг). */
esp_err_t wake_wakenet_init(void);

/* Освобождение ресурсов WakeNet. */
void wake_wakenet_deinit(void);

/* Для лога/диагностики. */
void wake_wakenet_get_info(wake_wakenet_info_t *out);

#ifdef __cplusplus
}
#endif


/* Возвращает true если wake detected. samples = число int16_t сэмплов (mono). */
bool wake_wakenet_detect(const int16_t *pcm, int samples);

/* Task: читает audio_stream и при wake вызывает voice_fsm_on_wake(). */
esp_err_t wake_wakenet_task_start(void);
void wake_wakenet_task_stop(void);
