#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t effect_id;
    uint8_t  brightness;   // 1..255 (0 = допустим, но обычно "выкл")
    uint16_t speed_pct;    // 10..300 (% от базовой скорости)
    bool     paused;
    uint32_t seq;          // увеличивается на каждое применённое изменение
} ctrl_state_t;

// Какие поля менять (чтобы кнопка не затирала speed/brightness и наоборот)
enum {
    CTRL_F_EFFECT     = (1u << 0),
    CTRL_F_BRIGHTNESS = (1u << 1),
    CTRL_F_SPEED      = (1u << 2),
    CTRL_F_PAUSED     = (1u << 3),
};

typedef enum {
    CTRL_CMD_SET_FIELDS = 0,     // payload: mask + значения
    CTRL_CMD_NEXT_EFFECT,
    CTRL_CMD_PREV_EFFECT,
    CTRL_CMD_PAUSE_TOGGLE,
    CTRL_CMD_ADJ_BRIGHTNESS,     // payload: int8 delta
    CTRL_CMD_ADJ_SPEED_PCT,      // payload: int16 delta
} ctrl_cmd_type_t;

typedef struct {
    ctrl_cmd_type_t type;
    uint8_t         field_mask;  // для SET_FIELDS
    uint16_t        effect_id;
    uint8_t         brightness;
    uint16_t        speed_pct;
    bool            paused;

    int8_t          delta_i8;    // для ADJ_BRIGHTNESS
    int16_t         delta_i16;   // для ADJ_SPEED_PCT
} ctrl_cmd_t;

esp_err_t ctrl_bus_init(void);
esp_err_t ctrl_bus_submit(const ctrl_cmd_t *cmd);

// Текущее состояние (для ACK)
void      ctrl_bus_get_state(ctrl_state_t *out);

#ifdef __cplusplus
}
#endif
