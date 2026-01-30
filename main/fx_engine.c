#include "fx_engine.h"
#include "fx_registry.h"

#include "matrix_ws2812.h"

#include "esp_log.h"

static const char *TAG = "FX_ENGINE";

static fx_ctx_t s_ctx;

void fx_engine_init(void)
{
    s_ctx.effect_id  = fx_registry_first_id();
    s_ctx.brightness = 102;
    s_ctx.speed_pct  = 100;
    s_ctx.paused     = false;

    // времена задаёт matrix_anim каждый кадр; тут просто обнулим
    s_ctx.wall_ms    = 0;
    s_ctx.wall_dt_ms = 0;
    s_ctx.anim_ms    = 0;
    s_ctx.anim_dt_ms = 0;

    matrix_ws2812_set_brightness(s_ctx.brightness);

    const fx_desc_t *d = fx_registry_get(s_ctx.effect_id);
    ESP_LOGI(TAG, "init ok (id=%u, name=%s)",
             (unsigned)s_ctx.effect_id,
             d ? d->name : "?");
}

void fx_engine_set_effect(uint16_t id)
{
    const fx_desc_t *d = fx_registry_get(id);
    if (!d) {
        ESP_LOGW(TAG, "effect=%u not found, fallback to first", (unsigned)id);
        s_ctx.effect_id = fx_registry_first_id();
        d = fx_registry_get(s_ctx.effect_id);
    } else {
        s_ctx.effect_id = id;
    }

    // ВАЖНО по NewTimeApproach:
    // anim_ms/anim_dt_ms обнуляются/пересчитываются в matrix_anim при смене эффекта.
    // fx_engine не управляет временем.

    ESP_LOGI(TAG, "effect=%u (%s)",
             (unsigned)s_ctx.effect_id,
             d ? d->name : "?");
}

void fx_engine_set_brightness(uint8_t b)
{
    s_ctx.brightness = b;
    matrix_ws2812_set_brightness(b);
}

void fx_engine_set_speed_pct(uint16_t spd_pct)
{
    if (spd_pct < 10)  spd_pct = 10;
    if (spd_pct > 300) spd_pct = 300;
    s_ctx.speed_pct = spd_pct;
}

void fx_engine_pause_set(bool paused)
{
    // Legacy API, но состояние пусть хранится (м.б. полезно для UI/диагностики).
    // Реальная пауза = anim_dt_ms==0, обеспечивается в matrix_anim.
    s_ctx.paused = paused;
}

uint16_t fx_engine_get_effect(void)     { return s_ctx.effect_id; }
uint8_t  fx_engine_get_brightness(void) { return s_ctx.brightness; }
uint16_t fx_engine_get_speed_pct(void)  { return s_ctx.speed_pct; }
bool     fx_engine_get_paused(void)     { return s_ctx.paused; }

void fx_engine_render(uint32_t wall_ms,
                      uint32_t wall_dt_ms,
                      uint32_t anim_ms,
                      uint32_t anim_dt_ms)
{
    // fx_engine — чистый consumer времени: просто прокидываем в ctx
    s_ctx.wall_ms    = wall_ms;
    s_ctx.wall_dt_ms = wall_dt_ms;
    s_ctx.anim_ms    = anim_ms;
    s_ctx.anim_dt_ms = anim_dt_ms;

    const fx_desc_t *d = fx_registry_get(s_ctx.effect_id);
    if (!d || !d->render) {
        // fallback: clear
        for (uint16_t y = 0; y < MATRIX_H; y++) {
            for (uint16_t x = 0; x < MATRIX_W; x++) {
                matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
            }
        }
        return;
    }

    // pause реализуется тем, что anim_dt_ms==0 (anim time frozen),
    // но render + show продолжают выполняться всегда (show делает matrix_anim).
    d->render(&s_ctx);
}
