#include "fx_engine.h"
#include "fx_canvas.h"
#include "matrix_ws2812.h"   // MATRIX_W, MATRIX_H + matrix_ws2812_set_pixel_xy()
#include "esp_random.h"

/* ============================================================
 * fx_effects_ported_1.c
 *
 * Пакет #1: эффекты, которым нужен framebuffer.
 * Зачем:
 *   - Проверяем "канву" (fade/shift/readback не нужен).
 *   - Готовим почву под Snow/Matrix/Fire и т.п.
 * ============================================================ */

/* Эффект 3: MATRIX RAIN (упрощённый)
 *  - каждый кадр слегка затемняем (trail)
 *  - сдвигаем вниз
 *  - на верхней строке случайно “зажигаем” зелёные капли
 */
void fx_matrix_rain_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    /* Чем выше speed_pct, тем меньше затухание (дольше след) */
    uint8_t dim = 220;
    if (ctx->speed_pct >= 200) dim = 235;
    if (ctx->speed_pct >= 260) dim = 245;

    fx_canvas_dim(dim);
    fx_canvas_shift_down(0, 0, 0);

    /* Плотность “капель” */
    uint8_t drops = 2;
    if (ctx->speed_pct >= 140) drops = 3;
    if (ctx->speed_pct >= 220) drops = 4;

    for (uint8_t i = 0; i < drops; i++) {
        const uint32_t r = esp_random();
        const uint16_t x = (uint16_t)(r % MATRIX_W);

        /* Яркость капли чуть плавает */
        const uint8_t g = (uint8_t)(180 + (r & 0x3F)); /* 180..243 */
        fx_canvas_set(x, 0, 0, g, 0);
    }

    fx_canvas_present();
}

/* Эффект 4: SNOW FALL (упрощённый)
 *  - fade
 *  - shift вниз
 *  - белые точки сверху
 */
void fx_snow_fall_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    uint8_t dim = 210;
    if (ctx->speed_pct >= 200) dim = 225;
    if (ctx->speed_pct >= 260) dim = 235;

    fx_canvas_dim(dim);
    fx_canvas_shift_down(0, 0, 0);

    uint8_t flakes = 2;
    if (ctx->speed_pct >= 140) flakes = 3;
    if (ctx->speed_pct >= 220) flakes = 4;

    for (uint8_t i = 0; i < flakes; i++) {
        const uint32_t r = esp_random();
        const uint16_t x = (uint16_t)(r % MATRIX_W);
        const uint8_t  v = (uint8_t)(200 + (r & 0x37)); /* около белого */
        fx_canvas_set(x, 0, v, v, v);
    }

    fx_canvas_present();
}

