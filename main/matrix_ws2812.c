#include "matrix_ws2812.h"

/*
 * matrix_ws2812.c
 *
 * Реализация драйвера матрицы WS2812 через ESP-IDF led_strip (RMT, DMA на ESP32-S3).
 *
 * Важные инварианты:
 *   - Яркость масштабируется софтверно (scale_bri), чтобы избежать внезапных токов на старте.
 *   - set_pixel_xy() пишет в буфер драйвера, show() отправляет буфер в линию WS2812.
 *   - static_one_pixel_test() предназначен для диагностики (один refresh и стоп).
 *
 * Риски / заметки:
 *   - Любой вызов show() инициирует передачу по RMT и потенциально создаёт нагрузку по питанию.
 *   - Если при static_one_pixel_test картинка "дрожит" без последующих refresh, ищем аппаратное:
 *     питание 5 V, GND, MOSFET low-side, level-shifter, помехи, длину data-линии, конденсаторы.
 */

#include <stdbool.h>      // bool
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "MATRIX_WS2812";

// Handle на led_strip (ESP-IDF managed component espressif/led_strip)
static led_strip_handle_t s_strip = NULL;

// Глобальная яркость 0..255. По умолчанию низкая (безопасный старт).
static uint8_t s_bri = 32u;

static inline uint8_t scale_bri(uint8_t v)
{
    // Масштабирование 0..255 с защитой от переполнения.
    return (uint8_t)(((uint16_t)v * (uint16_t)s_bri) / 255u);
}

uint16_t matrix_ws2812_xy_to_index(uint16_t x, uint16_t y)
{
    // На выходе всегда 0..(MATRIX_LEDS_TOTAL-1). Если координаты вне диапазона - возвращаем 0.
    // Примечание: вызывающие функции дополнительно могут игнорировать out-of-range.
    if (x >= MATRIX_W || y >= MATRIX_H) {
        return 0u;
    }

#if MATRIX_PANELS_HORIZONTAL
    // Панели стоят по X: [0..15] [16..31] [32..47], высота общая 0..15
    uint16_t panel = (uint16_t)(x / MATRIX_PANEL_W);  // 0..(MATRIX_PANELS-1)
    uint16_t lx    = (uint16_t)(x % MATRIX_PANEL_W);  // 0..15
    uint16_t ly    = y;                               // 0..15
#else
    // Панели стоят вертикально по Y
    uint16_t panel = (uint16_t)(y / MATRIX_PANEL_H);
    uint16_t lx    = x;
    uint16_t ly    = (uint16_t)(y % MATRIX_PANEL_H);
#endif

#if MATRIX_PANEL_ORDER_REVERSED
    panel = (uint16_t)(MATRIX_PANELS - 1u - panel);
#endif

    // Индекс внутри одной панели 16x16
#if MATRIX_SERPENTINE
    const bool row_even = ((ly & 1u) == 0u);
    const bool ltr = MATRIX_ROW0_LTR ? row_even : !row_even;
    const uint16_t local = (uint16_t)(ly * MATRIX_PANEL_W +
                              (ltr ? lx : (MATRIX_PANEL_W - 1u - lx)));
#else
    const uint16_t local = (uint16_t)(ly * MATRIX_PANEL_W + lx);
#endif

    return (uint16_t)(panel * MATRIX_PANEL_LEDS + local);
}

esp_err_t matrix_ws2812_init(gpio_num_t data_gpio)
{
    if (s_strip) {
        // Уже инициализировано
        return ESP_OK;
    }

    // Конфиг led_strip (RMT)
    const led_strip_config_t strip_config = {
        .strip_gpio_num = (int)data_gpio,
        .max_leds = MATRIX_LEDS_TOTAL,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // RMT config: 10 MHz resolution, DMA включаем для ESP32-S3
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10u * 1000u * 1000u, // 10 MHz
        .mem_block_symbols = 0,               // дефолт
        .flags.with_dma = true,
    };

    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return err;
    }

    // Стартуем в "известном" состоянии: буфер очищен + один refresh.
    matrix_ws2812_clear();
    const esp_err_t err2 = matrix_ws2812_show();

    ESP_LOGI(TAG, "WS2812 init ok: leds=%u, gpio=%d, bri=%u",
             (unsigned)MATRIX_LEDS_TOTAL, (int)data_gpio, (unsigned)s_bri);

    return err2;
}

void matrix_ws2812_deinit(void)
{
    if (!s_strip) return;
    led_strip_del(s_strip);
    s_strip = NULL;
}

void matrix_ws2812_set_brightness(uint8_t bri_0_255)
{
    // Прямое присваивание: 0..255
    s_bri = bri_0_255;
}

void matrix_ws2812_clear(void)
{
    if (!s_strip) return;
    (void)led_strip_clear(s_strip);
}

esp_err_t matrix_ws2812_show(void)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    return led_strip_refresh(s_strip);
}

void matrix_ws2812_set_pixel_xy(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;

    // Доп. защита: если координаты вне диапазона, не трогаем буфер.
    if (x >= MATRIX_W || y >= MATRIX_H) {
        return;
    }

    const uint16_t idx = matrix_ws2812_xy_to_index(x, y);

    // Яркость масштабируем сами, чтобы управлять токами и исключить "внезапную 100% яркость".
    (void)led_strip_set_pixel(s_strip, idx, scale_bri(r), scale_bri(g), scale_bri(b));
}

/* ============================================================
 *  WS2812 "STATIC ONE PIXEL" STERILE TEST
 *  - sets exactly one pixel
 *  - refresh exactly once
 *  - then never touches strip again (no further refresh)
 * ============================================================*/
void matrix_ws2812_static_one_pixel_test(uint16_t x, uint16_t y,
                                         uint8_t r, uint8_t g, uint8_t b,
                                         uint8_t bri_0_255)
{
    if (!s_strip) return;

    // Ограничим доступ по координатам, чтобы тест не "случайно" писал в idx=0.
    if (x >= MATRIX_W || y >= MATRIX_H) {
        ESP_LOGW(TAG, "static test: out of range x=%u y=%u", (unsigned)x, (unsigned)y);
        return;
    }

    // Фиксируем яркость на время теста.
    s_bri = bri_0_255;

    // Чистим буфер и ставим один пиксель
    (void)led_strip_clear(s_strip);
    {
        const uint16_t idx = matrix_ws2812_xy_to_index(x, y);
        (void)led_strip_set_pixel(s_strip, idx, scale_bri(r), scale_bri(g), scale_bri(b));
    }

    // ВАЖНО: refresh ровно один раз
    (void)led_strip_refresh(s_strip);

    // Дальше намеренно НИЧЕГО не делаем:
    // - не вызываем set_pixel
    // - не вызываем refresh
    // Если после этого "мерцает" — ищем питание/землю/ключ/уровни/помехи.
}
