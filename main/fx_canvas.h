#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * fx_canvas.h
 *
 * Зачем:
 *   - Даёт эффекторам "кадр" (RGB буфер) с чтением/записью.
 *   - Позволяет делать fade/shift и прочие штуки без readback из WS2812.
 *
 * Модель:
 *   - Эффект пишет в canvas.
 *   - В конце кадра вызывает fx_canvas_present(), который копирует в matrix_ws2812.
 * ============================================================ */

void fx_canvas_clear(uint8_t r, uint8_t g, uint8_t b);
void fx_canvas_set(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
bool fx_canvas_get(uint16_t x, uint16_t y, uint8_t *r, uint8_t *g, uint8_t *b);

/* Уменьшение яркости всех пикселей: scale=255 -> без изменений, 0 -> в ноль */
void fx_canvas_dim(uint8_t scale);

/* Сдвиги (простые утилиты для “дождя/снега/матрицы”) */
void fx_canvas_shift_down(uint8_t fill_r, uint8_t fill_g, uint8_t fill_b);
// Сдвиг к y=0 (вниз, к низу лампы). Новая верхняя строка (y=MATRIX_H-1) заполняется цветом.
void fx_canvas_shift_towards_y0(uint8_t r0, uint8_t g0, uint8_t b0);
void fx_canvas_shift_up(uint8_t fill_r, uint8_t fill_g, uint8_t fill_b);

/* Копирование canvas -> matrix_ws2812_set_pixel_xy(...) */
void fx_canvas_present(void);

#ifdef __cplusplus
}
#endif
