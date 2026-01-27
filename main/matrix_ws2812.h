#pragma once

/*
 * matrix_ws2812.h
 *
 * Назначение:
 *   Низкоуровневый драйвер "буфера кадра" для WS2812 (NeoPixel) в проекте Jinny Lamp.
 *   Отвечает за:
 *     - инициализацию драйвера ESP-IDF led_strip на RMT (с DMA на ESP32-S3),
 *     - перевод координат (x,y) -> индекс в цепочке светодиодов (учёт 3х панелей 16x16, "змейка"),
 *     - установку пикселей в буфер (без отправки на ленту),
 *     - отправку буфера на светодиоды (refresh/show),
 *     - статический "стерильный" тест одного пикселя (1 refresh и стоп).
 *
 * Важно / инварианты (проектные):
 *   - Мы масштабируем яркость сами (software scaling) для безопасного старта и контроля токов.
 *   - show()/refresh отправляет текущий буфер на WS2812.
 *   - Для поиска аппаратных проблем полезен static_one_pixel_test: один refresh и дальше тишина.
 *
 * Примечание по конфигу:
 *   Базовые макросы MATRIX_* можно переопределять через build flags или до include этого заголовка,
 *   если физическая раскладка панелей/змейки отличается.
 */

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

// ====== Конфиг матрицы (дефолт: 3 панели 16x16 по горизонтали = 48x16) ======
// ВАЖНО: базовые размеры теперь допускают переопределение извне через -D... или до include.

#ifndef MATRIX_PANEL_W
#define MATRIX_PANEL_W          16u
#endif

#ifndef MATRIX_PANEL_H
#define MATRIX_PANEL_H          16u
#endif

#ifndef MATRIX_PANELS
#define MATRIX_PANELS           3u
#endif

#ifndef MATRIX_PANELS_HORIZONTAL
#define MATRIX_PANELS_HORIZONTAL 0   // 0 = панели по Y (вертикально), 1 = по X (горизонтально)
#endif

#define MATRIX_PANEL_LEDS       (MATRIX_PANEL_W * MATRIX_PANEL_H)

#if MATRIX_PANELS_HORIZONTAL
#define MATRIX_W                (MATRIX_PANEL_W * MATRIX_PANELS)
#define MATRIX_H                (MATRIX_PANEL_H)
#else
#define MATRIX_W                (MATRIX_PANEL_W)
#define MATRIX_H                (MATRIX_PANEL_H * MATRIX_PANELS)
#endif

#define MATRIX_LEDS_TOTAL       (MATRIX_PANEL_LEDS * MATRIX_PANELS)


// Маппинг “змейкой” внутри каждой 16x16 (1 = serpentine, 0 = linear)
#ifndef MATRIX_SERPENTINE
#define MATRIX_SERPENTINE       1
#endif

// Ряд y=0 слева->направо (1) или справа->налево (0)
#ifndef MATRIX_ROW0_LTR
#define MATRIX_ROW0_LTR         1
#endif

// Если цепочка панелей физически идёт в обратном порядке (0..P-1 -> P-1..0)
#ifndef MATRIX_PANEL_ORDER_REVERSED
#define MATRIX_PANEL_ORDER_REVERSED 0
#endif
// ============================================================================

// Инициализация LED strip на заданном GPIO (WS2812 data). Повторный вызов безопасен.
esp_err_t matrix_ws2812_init(gpio_num_t data_gpio);

// Освободить ресурсы led_strip (если нужно для тестов/перезапуска).
void      matrix_ws2812_deinit(void);

// Установить яркость 0..255 (масштабирование выполняется софтверно при set_pixel).
void      matrix_ws2812_set_brightness(uint8_t bri_0_255);

// Очистить буфер (не отправляет на светодиоды до matrix_ws2812_show()).
void      matrix_ws2812_clear(void);

// Отправить текущий буфер на светодиоды (refresh).
esp_err_t matrix_ws2812_show(void);

// Установка пикселя по XY в общей системе координат (по умолчанию 48x16).
// Внимание: функция не вызывает show(); это только запись в буфер.
void      matrix_ws2812_set_pixel_xy(uint16_t x, uint16_t y,
                                     uint8_t r, uint8_t g, uint8_t b);

// Вспомогательное: XY->индекс в цепочке WS2812.
uint16_t  matrix_ws2812_xy_to_index(uint16_t x, uint16_t y);

/*
 * "Стерильный" тест:
 *   - очищает буфер
 *   - ставит ровно один пиксель
 *   - делает ровно один refresh
 *   - дальше намеренно НИЧЕГО не делает
 *
 * Если после этого видно мерцание/стробоскоп при полностью статичном изображении,
 * то это почти всегда питание/земля/ключ питания матриц/уровни/помехи, а не "частота обновления".
 */
void matrix_ws2812_static_one_pixel_test(uint16_t x, uint16_t y,
                                         uint8_t r, uint8_t g, uint8_t b,
                                         uint8_t bri_0_255);
