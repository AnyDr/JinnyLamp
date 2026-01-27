#include "fx_canvas.h"

#include "matrix_ws2812.h"
#include "esp_log.h"

static const char *TAG = "FX_CANVAS";

/* RGB буфер: 3 байта на пиксель */
static uint8_t s_buf[(uint32_t)MATRIX_W * (uint32_t)MATRIX_H * 3u];

static inline uint32_t idx_of(uint16_t x, uint16_t y)
{
    return ((uint32_t)y * (uint32_t)MATRIX_W + (uint32_t)x) * 3u;
}

void fx_canvas_clear(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint32_t i = idx_of(x, y);
            s_buf[i + 0] = r;
            s_buf[i + 1] = g;
            s_buf[i + 2] = b;
        }
    }
}

void fx_canvas_set(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x >= MATRIX_W || y >= MATRIX_H) return;
    const uint32_t i = idx_of(x, y);
    s_buf[i + 0] = r;
    s_buf[i + 1] = g;
    s_buf[i + 2] = b;
}

bool fx_canvas_get(uint16_t x, uint16_t y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (x >= MATRIX_W || y >= MATRIX_H) return false;
    const uint32_t i = idx_of(x, y);
    if (r) *r = s_buf[i + 0];
    if (g) *g = s_buf[i + 1];
    if (b) *b = s_buf[i + 2];
    return true;
}

void fx_canvas_dim(uint8_t scale)
{
    /* scale 0..255: (v*scale)/255 */
    const uint32_t n = (uint32_t)MATRIX_W * (uint32_t)MATRIX_H * 3u;
    for (uint32_t i = 0; i < n; i++) {
        const uint16_t v = s_buf[i];
        s_buf[i] = (uint8_t)((v * (uint16_t)scale) / 255u);
    }
}

void fx_canvas_shift_down(uint8_t fill_r, uint8_t fill_g, uint8_t fill_b)
{
    if (MATRIX_H == 0 || MATRIX_W == 0) return;

    for (int y = (int)MATRIX_H - 1; y >= 1; y--) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            uint8_t r, g, b;
            (void)fx_canvas_get(x, (uint16_t)(y - 1), &r, &g, &b);
            fx_canvas_set(x, (uint16_t)y, r, g, b);
        }
    }

    /* top row fill */
    for (uint16_t x = 0; x < MATRIX_W; x++) {
        fx_canvas_set(x, 0, fill_r, fill_g, fill_b);
    }
}

void fx_canvas_shift_towards_y0(uint8_t r0, uint8_t g0, uint8_t b0)
{
    // Двигаем всё к меньшим y: dest[y] = src[y+1]
    for (int y = 0; y < (int)MATRIX_H - 1; y++) {
        for (int x = 0; x < (int)MATRIX_W; x++) {
            uint8_t r, g, b;
            fx_canvas_get((uint16_t)x, (uint16_t)(y + 1), &r, &g, &b);
            fx_canvas_set((uint16_t)x, (uint16_t)y, r, g, b);
        }
    }

    // Верхнюю строку заполняем фоном
    for (int x = 0; x < (int)MATRIX_W; x++) {
        fx_canvas_set((uint16_t)x, (uint16_t)(MATRIX_H - 1), r0, g0, b0);
    }
}


void fx_canvas_shift_up(uint8_t fill_r, uint8_t fill_g, uint8_t fill_b)
{
    if (MATRIX_H == 0 || MATRIX_W == 0) return;

    for (uint16_t y = 0; y + 1 < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            uint8_t r, g, b;
            (void)fx_canvas_get(x, (uint16_t)(y + 1), &r, &g, &b);
            fx_canvas_set(x, y, r, g, b);
        }
    }

    /* bottom row fill */
    for (uint16_t x = 0; x < MATRIX_W; x++) {
        fx_canvas_set(x, (uint16_t)(MATRIX_H - 1), fill_r, fill_g, fill_b);
    }
}

void fx_canvas_present(void)
{
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint32_t i = idx_of(x, y);
            matrix_ws2812_set_pixel_xy(x, y, s_buf[i + 0], s_buf[i + 1], s_buf[i + 2]);
        }
    }
}
