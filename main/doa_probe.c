#include "doa_probe.h"

#include <math.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "xvf_i2c.h"

/*
 * DOA Probe (service)
 *
 * Цель:
 * - Постоянно (always-on) опрашивать XVF3800 по I2C и держать актуальный DOA snapshot.
 * - Данные должны быть доступны любым компонентам через doa_probe_get_snapshot().
 *
 * Debug:
 * - Лог в монитор включается/выключается через j_doa_debug_log_enabled(), которая живёт
 *   в fx_effects_doa_debug.c и управляется одним define в этом файле.
 * - Никаких CONFIG_J_DOA_DEBUG / sdkconfig.h тут больше нет.
 *
 * Важно:
 * - "Нет речи" обычно = NaN или не-updated; мы не обновляем snapshot если значение не finite.
 * - При сбое I2C snapshot не обновляется, age_ms растёт, и компоненты могут делать fade.
 */


/* ------------------------------ Config ------------------------------ */

// AEC_AZIMUTH_VALUES: resid=33 cmd=75 payload=16 (4 floats, radians)
// [0]=beam1 [1]=beam2 [2]=free-running [3]=auto-select beam
#define XVF_RESID_AEC_AZIMUTH_VALUES   33u
#define XVF_CMD_AEC_AZIMUTH_VALUES     75u
#define XVF_LEN_AEC_AZIMUTH_VALUES     16u
#define AEC_AZIMUTH_IDX_AUTOSELECT     3u

// Poll rate: 10 Hz as per project target
#define DOA_POLL_PERIOD_MS             100u

// Logging throttle (when debug log is enabled)
#define DOA_LOG_PERIOD_MS              1000u

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


/* ------------------------------ External debug toggle ------------------------------ */

// single source of truth in fx_effects_doa_debug.c (define-driven)
bool j_doa_debug_log_enabled(void);


/* ------------------------------ Local helpers ------------------------------ */

static const char *TAG = "DOA_PROBE";

static uint32_t rd_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static float u32_to_f32(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static float wrap_rad_0_2pi(float rad)
{
    if (!isfinite(rad)) return rad;

    const float two_pi = 2.0f * (float)M_PI;

    // fmodf can return negative
    rad = fmodf(rad, two_pi);
    if (rad < 0.0f) rad += two_pi;
    return rad;
}

static float rad_to_deg(float rad)
{
    return rad * (180.0f / (float)M_PI);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}


/* ------------------------------ State ------------------------------ */

// Snapshot protection (fast + reliable)
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static doa_snapshot_t s_last = {0};
static uint32_t       s_last_update_ms = 0;

static bool s_started = false;

/*
 * Legacy debug flag API.
 * Ранее debug мог включаться из меню, сейчас debug управляется define через j_doa_debug_log_enabled().
 * Мы сохраняем API для совместимости, но логика в task опирается только на j_doa_debug_log_enabled().
 */
static bool s_debug = false;


static void store_snapshot(float az_rad_wrapped, uint8_t status)
{
    const uint32_t t = now_ms();

    portENTER_CRITICAL(&s_mux);
    s_last.azimuth_rad = az_rad_wrapped;
    s_last.azimuth_deg = rad_to_deg(az_rad_wrapped);
    s_last.status      = status;
    s_last.valid       = isfinite(az_rad_wrapped);
    s_last_update_ms   = t;
    portEXIT_CRITICAL(&s_mux);
}

bool doa_probe_get_snapshot(doa_snapshot_t *out)
{
    doa_snapshot_t tmp;
    uint32_t last_t;

    portENTER_CRITICAL(&s_mux);
    tmp    = s_last;
    last_t = s_last_update_ms;
    portEXIT_CRITICAL(&s_mux);

    if (!tmp.valid) {
        if (out) *out = tmp;
        return false;
    }

    const uint32_t t = now_ms();
    tmp.age_ms = (t >= last_t) ? (t - last_t) : 0;

    if (out) *out = tmp;
    return true;
}

void doa_probe_set_debug(bool enable)
{
    // Legacy hook (kept). Not used for log gating anymore.
    s_debug = enable;
}

bool doa_probe_get_debug(void)
{
    return s_debug;
}


/* ------------------------------ Task ------------------------------ */

static void doa_probe_task(void *arg)
{
    (void)arg;

    uint8_t status = 0;
    uint8_t raw[XVF_LEN_AEC_AZIMUTH_VALUES];

    uint32_t last_log_ms = 0;

    for (;;) {
        memset(raw, 0, sizeof(raw));

        const esp_err_t err = xvf_read_payload(
            XVF_RESID_AEC_AZIMUTH_VALUES,
            XVF_CMD_AEC_AZIMUTH_VALUES,
            raw,
            XVF_LEN_AEC_AZIMUTH_VALUES,
            &status
        );

        if (err == ESP_OK) {
            // Decode float32 LE for auto-selected beam
            const uint8_t *p = &raw[4u * AEC_AZIMUTH_IDX_AUTOSELECT];
            const float rad = u32_to_f32(rd_u32_le(p));
            const float rad_wrapped = wrap_rad_0_2pi(rad);

            if (isfinite(rad_wrapped)) {
                store_snapshot(rad_wrapped, status);
            }

            // Debug log (throttled)
            if (j_doa_debug_log_enabled()) {
                const uint32_t t = now_ms();
                if ((t - last_log_ms) >= DOA_LOG_PERIOD_MS) {
                    doa_snapshot_t s;
                    (void)doa_probe_get_snapshot(&s);

                    ESP_LOGI(TAG,
                             "AEC_AZIMUTH auto: deg=%.2f rad=%.6f age=%ums status=0x%02X",
                             (double)s.azimuth_deg,
                             (double)s.azimuth_rad,
                             (unsigned)s.age_ms,
                             (unsigned)s.status);

                    last_log_ms = t;
                }
            }

        } else {
            // Debug warning on I2C/protocol error
            if (j_doa_debug_log_enabled()) {
                ESP_LOGW(TAG, "read AEC_AZIMUTH_VALUES err=%s", esp_err_to_name(err));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DOA_POLL_PERIOD_MS));
    }
}

void doa_probe_start(void)
{
    if (s_started) return;
    s_started = true;

    const BaseType_t ok = xTaskCreate(doa_probe_task, "doa_probe", 4096, NULL, 5, NULL);
    if (ok != pdPASS) {
        // If task creation failed, show it even with debug off.
        ESP_LOGE(TAG, "xTaskCreate(doa_probe) failed");
        s_started = false;
    }
}
