#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_ctx {
    // текущие параметры (истина приходит из ctrl_bus)
    uint16_t effect_id;
    uint8_t  brightness;
    uint16_t speed_pct;     // 10..300
    bool     paused;

    // внутреннее состояние
    uint32_t phase;         // общий аккумулятор
    uint32_t frame;         // счётчик кадров
    uint8_t  base_step;     // из registry
} fx_ctx_t;

void fx_engine_init(void);

void fx_engine_set_effect(uint16_t id);
void fx_engine_set_brightness(uint8_t b);
void fx_engine_set_speed_pct(uint16_t spd_pct);
void fx_engine_pause_set(bool paused);

uint16_t fx_engine_get_effect(void);
uint8_t  fx_engine_get_brightness(void);
uint16_t fx_engine_get_speed_pct(void);
bool     fx_engine_get_paused(void);

// Рендер одного кадра. t_ms можно получать из tick_count*period_ms.
void fx_engine_render(uint32_t t_ms);

#ifdef __cplusplus
}
#endif
