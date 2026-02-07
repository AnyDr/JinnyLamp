#include "genie_overlay.h"

#include "esp_log.h"

#include "doa_probe.h"
#include "matrix_ws2812.h"


/* NOTE: функция реализована в matrix_ws2812.c, но не объявлена в matrix_ws2812.h.
 * Минимально-инвазивно объявляем здесь. */
extern void matrix_ws2812_set_pixel_xy(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);


static const char *TAG = "GENIE_OVR";

static bool s_enabled = true;

void genie_overlay_init(void)
{
    ESP_LOGI(TAG, "genie overlay init");
}

void genie_overlay_set_enabled(bool en)
{
    s_enabled = en;
}

bool genie_overlay_is_enabled(void)
{
    return s_enabled;
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* MVP: 1 пиксель. X = DOA (0..360)->0..15. Y = середина по высоте. */
void genie_overlay_render(uint32_t now_ms)
{
    if (!s_enabled) return;

    doa_snapshot_t s;
    if (!doa_probe_get_snapshot(&s)) {
        return;
    }

    if (!s.valid) {
        return;
    }

    if (s.age_ms > 500) { // старые данные не рисуем
        return;
    }

    const int w = (int)MATRIX_W;
    const int h = (int)MATRIX_H;

    /* 0..360 -> 0..w-1 */
    float deg = s.azimuth_deg;
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;

    int x = (int)((deg * (float)(w - 1)) / 360.0f + 0.5f);
    if (x < 0) x = 0;
    if (x >= w) x = w - 1;

    int y = h / 2;

    /* Цвет: мягкий cyan, потом заменим на “рот/лицо”. */
    uint8_t r = 0;
    uint8_t g = 60;
    uint8_t b = 120;

    /* Рисуем поверх текущего кадра в canvas */
    matrix_ws2812_set_pixel_xy((uint16_t)x, (uint16_t)y, r, g, b);

}
