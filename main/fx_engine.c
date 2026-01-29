#include "fx_engine.h"
#include "fx_registry.h"

#include "matrix_ws2812.h"

#include "esp_log.h"
#include "esp_system.h"  
#include "esp_random.h" // esp_random()

static const char *TAG = "FX_ENGINE";
#define FX_PHASE_FPS_REF   20u  // историческая “база” для base_step (phase per frame @20 FPS)

static fx_ctx_t s_ctx;


void fx_engine_init(void)
{
    s_ctx.effect_id  = fx_registry_first_id();
    s_ctx.brightness = 102;
    s_ctx.speed_pct  = 100;
    s_ctx.paused     = false;
    s_ctx.phase      = 0;
    s_ctx.frame      = 0;
    s_ctx.base_step  = 4;

    // Подтянуть base_step из registry, если есть
    const fx_desc_t *d = fx_registry_get(s_ctx.effect_id);
    if (d) {
        s_ctx.base_step = d->base_step;
    }

    matrix_ws2812_set_brightness(s_ctx.brightness);
    ESP_LOGI(TAG, "init ok (id=%u)", (unsigned)s_ctx.effect_id);
}

void fx_engine_set_effect(uint16_t id)
{
    s_ctx.effect_id = id;

    const fx_desc_t *d = fx_registry_get(id);
    if (d) {
        s_ctx.base_step = d->base_step;
        ESP_LOGI(TAG, "effect=%u (%s)", (unsigned)id, d->name);
    } else {
        ESP_LOGW(TAG, "effect=%u not found, fallback to first", (unsigned)id);
        s_ctx.effect_id = fx_registry_first_id();
        d = fx_registry_get(s_ctx.effect_id);
        s_ctx.base_step = d ? d->base_step : 4;
    }

    // при смене эффекта сбрасываем фазу/кадр — предсказуемо
    s_ctx.phase = 0;
    s_ctx.frame = 0;
}

void fx_engine_set_brightness(uint8_t b)
{
    s_ctx.brightness = b;
    matrix_ws2812_set_brightness(b);
}

void fx_engine_set_speed_pct(uint16_t spd_pct)
{
    if (spd_pct < 10) spd_pct = 10;
    if (spd_pct > 300) spd_pct = 300;
    s_ctx.speed_pct = spd_pct;
}

void fx_engine_pause_set(bool paused)
{
    // Legacy API.
    // Реальная пауза управляется через anim_dt_ms == 0 в matrix_anim.
    s_ctx.paused = paused;
}


uint16_t fx_engine_get_effect(void)      { return s_ctx.effect_id; }
uint8_t  fx_engine_get_brightness(void)  { return s_ctx.brightness; }
uint16_t fx_engine_get_speed_pct(void)   { return s_ctx.speed_pct; }
bool     fx_engine_get_paused(void)      { return s_ctx.paused; }

void fx_engine_render(uint32_t wall_ms,
                      uint32_t wall_dt_ms,
                      uint32_t anim_ms,
                      uint32_t anim_dt_ms)
{
    (void)wall_dt_ms;

    // Прокидываем времена в ctx, чтобы эффекты (особенно DOA) могли их читать уже сейчас,
    // даже до полной миграции FX.
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

    // ВАЖНО (по ТЗ):
    // - pause реализуется тем, что anim_dt_ms==0 (anim time frozen),
    // - но render + show продолжают выполняться всегда.
    //
    // Переходный слой совместимости:
    // старые эффекты опираются на phase/frame. Сделаем их time-based,
    // чтобы скорость не зависела от FPS.

    if (anim_dt_ms != 0) {
        // base_step historically meant “phase per frame @ 20 FPS (and speed_pct=100)”.
        // Переводим в “phase per ms”.
        //
        // phase_inc = anim_dt_ms * base_step * speed_pct / (100 * frame_ms_ref)
        // frame_ms_ref = 1000 / FX_PHASE_FPS_REF => denom = 100 * 1000 / FPS_REF
        const uint32_t denom = (100u * (1000u / FX_PHASE_FPS_REF)); // 100*50=5000 for 20 FPS
        const uint32_t num =
        (uint32_t)anim_dt_ms * (uint32_t)s_ctx.base_step;



        uint32_t inc = num / denom;
        if (inc == 0) inc = 1;

        s_ctx.phase += inc;
        s_ctx.frame++;
    }

    // Пока ещё вызываем legacy-сигнатуру render(ctx, t_ms).
    // В time-based мире “t_ms для анимации” = anim_ms.
    d->render(&s_ctx, anim_ms);
}


/* ============================================================
 * Первые 3 портированных эффекта (минимальные, для проверки каркаса)
 * Позже вынесем их в fx_effects_basic.c и начнём порт пачками.
 * ============================================================ */

static void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // простая integer HSV->RGB (достаточно для тестов)
    const uint8_t region = h / 43;
    const uint8_t rem = (h - region * 43) * 6;

    const uint8_t p = (uint16_t)(v) * (255 - s) / 255;
    const uint8_t q = (uint16_t)(v) * (255 - (uint16_t)s * rem / 255) / 255;
    const uint8_t t = (uint16_t)(v) * (255 - (uint16_t)s * (255 - rem) / 255) / 255;

    switch (region) {
        default:
        case 0: *r=v; *g=t; *b=p; break;
        case 1: *r=q; *g=v; *b=p; break;
        case 2: *r=p; *g=v; *b=t; break;
        case 3: *r=p; *g=q; *b=v; break;
        case 4: *r=t; *g=p; *b=v; break;
        case 5: *r=v; *g=p; *b=q; break;
    }
}

void fx_solid_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;
    const uint8_t hue = (uint8_t)(ctx->phase & 0xFFu);

    uint8_t r,g,b;
    hsv2rgb(hue, 255, 255, &r, &g, &b);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

void fx_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;
    const uint8_t base = (uint8_t)(ctx->phase & 0xFFu);

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint8_t hue = (uint8_t)(base + x * 4u + y * 6u);
            uint8_t r,g,b;
            hsv2rgb(hue, 255, 255, &r, &g, &b);
            matrix_ws2812_set_pixel_xy(x, y, r, g, b);
        }
    }
}

void fx_sparkles_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    // ВАЖНО: при "настоящей паузе" fx_engine_render() не вызывает render().
    // Значит тут можно спокойно использовать esp_random(), не боясь "анти-паузы".

    // фон чёрный (упрощённо; позже сделаем framebuffer для fade)
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            matrix_ws2812_set_pixel_xy(x, y, 0, 0, 0);
        }
    }

    // число искр зависит от speed_pct
    const uint16_t n = (ctx->speed_pct >= 250) ? 12 : (ctx->speed_pct >= 150 ? 8 : 5);

    for (uint16_t i = 0; i < n; i++) {
        const uint32_t rnd = esp_random();
        const uint16_t x = (uint16_t)(rnd % MATRIX_W);
        const uint16_t y = (uint16_t)((rnd >> 8) % MATRIX_H);

        const uint8_t hue = (uint8_t)(ctx->phase + (rnd >> 16));
        uint8_t r,g,b;
        hsv2rgb(hue, 40, 255, &r, &g, &b);

        matrix_ws2812_set_pixel_xy(x, y, r, g, b);
    }
}
