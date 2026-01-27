#include "sdkconfig.h"

#if CONFIG_J_DOA_DEBUG

#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "xvf_i2c.h"

static const char *TAG = "DOA_PROBE";

// AEC_AZIMUTH_VALUES: resid=33 cmd=75 payload=16 (4 floats)
#define XVF_RESID_AEC_AZIMUTH_VALUES   33u
#define XVF_CMD_AEC_AZIMUTH_VALUES     75u
#define XVF_LEN_AEC_AZIMUTH_VALUES     16u

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float u32_to_f32(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t rd_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[3]) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[0] << 24);
}

static void doa_probe_task(void *arg)
{
    (void)arg;

    uint8_t status = 0;
    uint8_t raw[16];

    for (;;) {
        memset(raw, 0, sizeof(raw));

        esp_err_t err = xvf_read_payload(
            XVF_RESID_AEC_AZIMUTH_VALUES,
            XVF_CMD_AEC_AZIMUTH_VALUES,
            raw,
            XVF_LEN_AEC_AZIMUTH_VALUES,
            &status
        );

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read err=%s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // raw bytes
        ESP_LOGI(TAG, "status=%u raw=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned)status,
                 raw[0], raw[1], raw[2], raw[3],
                 raw[4], raw[5], raw[6], raw[7],
                 raw[8], raw[9], raw[10], raw[11],
                 raw[12], raw[13], raw[14], raw[15]);

        // decode as 4 floats (LE and BE) + degrees
        for (int i = 0; i < 4; i++) {
            const uint8_t *p = &raw[i * 4];
            const float f_le = u32_to_f32(rd_u32_le(p));
            const float f_be = u32_to_f32(rd_u32_be(p));

            const float deg_le = f_le * (180.0f / (float)M_PI);
            const float deg_be = f_be * (180.0f / (float)M_PI);

            ESP_LOGI(TAG, "idx=%d LE: rad=%.6f deg=%.2f | BE: rad=%.6f deg=%.2f",
                     i, (double)f_le, (double)deg_le, (double)f_be, (double)deg_be);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void doa_probe_start(void)
{
    const BaseType_t ok = xTaskCreate(doa_probe_task, "doa_probe", 4096, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(doa_probe) failed");
    }
}

#else

void doa_probe_start(void) { }

#endif // CONFIG_J_DOA_DEBUG
