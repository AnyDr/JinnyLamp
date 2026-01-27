// fx_effects_doa_debug.c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_err.h"

#include "fx_engine.h"
#include "fx_canvas.h"
#include "matrix_ws2812.h"
#include "xvf_i2c.h"
#include "asr_debug.h"

static const char *TAG = "FX_DOA_DBG";

/* ---- Настройки маппинга (потом вынесем в DOA_ctrl как и планировали) ---- */
#ifndef DOA_OFFSET_DEG
#define DOA_OFFSET_DEG   0.0f
#endif

#ifndef DOA_INV
#define DOA_INV          0
#endif

/* Уровень (level из asr_debug) в процентах "на сколько % громче шума" */
#ifndef DOA_LEVEL_MIN
#define DOA_LEVEL_MIN    2u     // ниже этого гасим/затухаем
#endif

#ifndef DOA_LEVEL_FULL
#define DOA_LEVEL_FULL   80u    // при таком level индикатор на максимальной высоте
#endif

/* XVF параметр AEC_AZIMUTH_VALUES:
   В seeed-скриптах встречается resid=33, cmd=75, чтение cmd = 0x80|75. :contentReference[oaicite:0]{index=0} */
#define XVF_RESID_AEC_AZIMUTH_VALUES   33u
#define XVF_CMD_AEC_AZIMUTH_VALUES     75u

/* -------- float helpers: LE/BE decode without assuming host endian -------- */
static float f32_from_u32(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static float f32_from_le(const uint8_t b[4])
{
    const uint32_t u = ((uint32_t)b[0]) |
                       ((uint32_t)b[1] << 8) |
                       ((uint32_t)b[2] << 16) |
                       ((uint32_t)b[3] << 24);
    return f32_from_u32(u);
}

static float f32_from_be(const uint8_t b[4])
{
    const uint32_t u = ((uint32_t)b[3]) |
                       ((uint32_t)b[2] << 8) |
                       ((uint32_t)b[1] << 16) |
                       ((uint32_t)b[0] << 24);
    return f32_from_u32(u);
}

static bool rad_plausible(float r)
{
    if (!isfinite(r)) return false;
    /* допускаем небольшой запас, т.к. встречаются значения вне 0..2π в шуме */
    return (r > -1.0f) && (r < (2.0f * 3.14159265f + 1.0f));
}

static float pick_azimuth_rad(const uint8_t payload16[16])
{
    float le[4];
    float be[4];

    for (int i = 0; i < 4; i++) {
        le[i] = f32_from_le(&payload16[i * 4]);
        be[i] = f32_from_be(&payload16[i * 4]);
    }

    int le_ok = 0, be_ok = 0;
    for (int i = 0; i < 4; i++) {
        if (rad_plausible(le[i])) le_ok++;
        if (rad_plausible(be[i])) be_ok++;
    }

    // Choose endian by plausibility (empirical proof from logs -> should be LE)
    const float *src = (le_ok >= be_ok) ? le : be;

    // Prefer channels that actually move (your logs: idx2/3 often stuck at pi/2)
    const float pi2 = 1.57079633f;
    const float eps = 0.02f; // ~1.1 deg

    int best = -1;

    // Priority: 0 -> 1 -> 2 -> 3
    for (int i = 0; i < 4; i++) {
        if (!rad_plausible(src[i])) continue;
        best = i;
        break;
    }

    if (best < 0) {
        // Nothing plausible -> return NaN, caller will ignore
        return NAN;
    }

    // If chosen value is ~pi/2, but there exists another plausible one != pi/2, prefer that one
    if (fabsf(src[best] - pi2) <= eps) {
        for (int i = 0; i < 4; i++) {
            if (!rad_plausible(src[i])) continue;
            if (fabsf(src[i] - pi2) > eps) {
                best = i;
                break;
            }
        }
    }

    return src[best];
}


static float norm_deg(float deg)
{
    while (deg < 0.0f)   deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    return deg;
}

/* ===========================================================
 * FX: DOA Debug
 *  - 1 пиксель
 *  - X: угол (0..360) -> 0..MATRIX_W-1
 *  - Y: level (asr_debug) -> 0..MATRIX_H-1 (0 = низ)
 *  - при низком уровне: затухание до нуля
 * =========================================================== */
void fx_doa_debug_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)t_ms;

    /* мягкое затухание хвоста */
    fx_canvas_dim(235);

    const uint16_t level = asr_debug_get_level(); /* % над noise_floor */
    if (level < DOA_LEVEL_MIN) {
        fx_canvas_present();
        return;
    }

    uint8_t status = 0;
    uint8_t payload[16] = {0};

    const uint8_t cmd_read = (uint8_t)(0x80u | (uint8_t)XVF_CMD_AEC_AZIMUTH_VALUES);
    const esp_err_t err = xvf_read_payload((uint8_t)XVF_RESID_AEC_AZIMUTH_VALUES,
                                          cmd_read,
                                          payload,
                                          sizeof(payload),
                                          &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "xvf_read_payload err=%s", esp_err_to_name(err));
        fx_canvas_present();
        return;
    }

    const float rad = pick_azimuth_rad(payload);
    if (!isfinite(rad)) {
        fx_canvas_present();
        return;
    }

    float deg = (rad * (180.0f / 3.14159265f));
    deg = norm_deg(deg + DOA_OFFSET_DEG);

#if DOA_INV
    deg = norm_deg(360.0f - deg);
#endif

    /* X: 0..360 -> 0..MATRIX_W-1 */
    uint16_t x = (uint16_t)((deg * (float)MATRIX_W) / 360.0f);
    if (x >= MATRIX_W) x = (uint16_t)(MATRIX_W - 1);

    /* Y: level -> 0..MATRIX_H-1, 0 = низ */
    uint16_t lvl = level;
    if (lvl > DOA_LEVEL_FULL) lvl = DOA_LEVEL_FULL;

    uint16_t y = (uint16_t)(((uint32_t)lvl * (uint32_t)(MATRIX_H - 1)) / DOA_LEVEL_FULL);

    /* цвет/яркость: завязка на brightness из ctx */
    uint8_t v = ctx->brightness;
    if (v < 16) v = 16;

    fx_canvas_set(x, y, v, v, v);
    fx_canvas_present();
}
