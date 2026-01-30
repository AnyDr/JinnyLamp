#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_ctx_t {
    // Controls (single source of truth lives in ctrl_bus)
    uint16_t effect_id;
    uint8_t  brightness;   // 0..255
    uint16_t speed_pct;    // 10..300 (но скорость уже применена в anim_dt_ms) (NOTE: anim_dt_ms already includes speed scaling; do NOT re-scale time in effects)
    bool     paused;

    // Master-clock times (set by matrix_anim each frame)
    uint32_t wall_ms;
    uint32_t wall_dt_ms;
    uint32_t anim_ms;
    uint32_t anim_dt_ms;
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

// Render одного кадра.
// wall_*  — реальное время (не зависит от pause)
// anim_*  — время анимации (масштабируется speed_pct, замораживается при pause, сбрасывается при смене эффекта)
void fx_engine_render(
    uint32_t wall_ms,
    uint32_t wall_dt_ms,
    uint32_t anim_ms,
    uint32_t anim_dt_ms
);


#ifdef __cplusplus
}
#endif
