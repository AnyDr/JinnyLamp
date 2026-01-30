#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  volume_pct;   // 0..100
    uint32_t seq;          // увеличивается на каждое применённое изменение
} audio_state_t;

typedef enum {
    AUDIO_CMD_SET_VOLUME = 0,
    AUDIO_CMD_GET_STATE,
} audio_cmd_type_t;

typedef struct {
    audio_cmd_type_t type;
    uint8_t          volume_pct;   // для SET_VOLUME
} audio_cmd_t;

esp_err_t audio_bus_init(void);
esp_err_t audio_bus_submit(const audio_cmd_t *cmd);
void      audio_bus_get_state(audio_state_t *out);

#ifdef __cplusplus
}
#endif
