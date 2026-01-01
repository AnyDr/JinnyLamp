#include <stdint.h>
#include <stdbool.h>

#include "fx_engine.h"
#include "matrix_ws2812.h"

/* ============================================================
 * fx_effects_noise_1.c
 *
 * Назначение:
 *   Порт “шумовых” эффектов из Arduino/FastLED (noiseEffects.ino)
 *   в архитектуру Jinny Lamp:
 *     fx_registry -> fx_engine_render() -> matrix_ws2812_set_pixel_xy()
 *
 * Принцип:
 *   - Генерируем 2D value-noise (8-bit) с плавной интерполяцией.
 *   - Значение 0..255 маппим в палитру (16 опорных цветов + интерполяция).
 *
 * Почему так:
 *   - Похоже визуально на FastLED noise-палитры.
 *   - Ноль динамической памяти.
 *   - Детеминированно, не зависит от ISR/очередей.
 *   - Для нашей матрицы 48x16 (768 пикселей) нагрузка очень умеренная.
 * ============================================================ */

typedef struct {
    uint8_t r, g, b;
} rgb8_t;

/* ---------------- Простые math helpers ---------------- */

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t t)
{
    /* t: 0..255 */
    return (uint8_t)(a + (uint16_t)(b - a) * (uint16_t)t / 255u);
}

static inline rgb8_t lerp_rgb(rgb8_t a, rgb8_t b, uint8_t t)
{
    rgb8_t o = {
        .r = lerp_u8(a.r, b.r, t),
        .g = lerp_u8(a.g, b.g, t),
        .b = lerp_u8(a.b, b.b, t),
    };
    return o;
}

/* palette16_sample:
 *   idx 0..255 -> берём сегмент 0..15, интерполируем к следующему
 *   (как “градиентная” палитра FastLED)
 */
static rgb8_t palette16_sample(const rgb8_t pal[16], uint8_t idx)
{
    const uint8_t seg  = (uint8_t)(idx >> 4);               /* 0..15 */
    const uint8_t frac = (uint8_t)((idx & 0x0Fu) * 17u);    /* 0..255 (0,17,...255) */

    const rgb8_t a = pal[seg];
    const rgb8_t b = pal[(uint8_t)((seg + 1u) & 0x0Fu)];
    return lerp_rgb(a, b, frac);
}

/* ---------------- 2D value-noise (8.8 fixed point) ---------------- */

/* Небольшая хэш-функция: быстро, детерминированно, “достаточно шумно” */
static inline uint8_t hash8_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (uint8_t)(x >> 24);
}

/* fade_u8:
 *   smoothstep(t) = t^2*(3-2t), t в диапазоне 0..255, без float
 */
static inline uint8_t fade_u8(uint8_t t)
{
    const uint16_t tt = (uint16_t)t * (uint16_t)t;          /* 0..65025 */
    const uint16_t t2 = (uint16_t)((3u * 255u) - (2u * t)); /* 0..765 */
    const uint32_t v  = (uint32_t)tt * (uint32_t)t2;
    return (uint8_t)(v / (255u * 255u));                    /* обратно в 0..255 */
}

static inline uint8_t lerp_noise_u8(uint8_t a, uint8_t b, uint8_t t)
{
    return (uint8_t)(a + (uint16_t)(b - a) * (uint16_t)t / 255u);
}

/* noise2d_88:
 *   x_88, y_88: координаты 8.8 (старший байт = номер клетки)
 *   seed: меняем во времени, чтобы “двигать” текстуру
 */
static uint8_t noise2d_88(uint16_t x_88, uint16_t y_88, uint32_t seed)
{
    const uint8_t xi = (uint8_t)(x_88 >> 8);
    const uint8_t yi = (uint8_t)(y_88 >> 8);
    const uint8_t xf = (uint8_t)(x_88 & 0xFF);
    const uint8_t yf = (uint8_t)(y_88 & 0xFF);

    const uint8_t u = fade_u8(xf);
    const uint8_t v = fade_u8(yf);

    const uint8_t n00 = hash8_u32(seed ^ ((uint32_t)xi * 374761393u) ^ ((uint32_t)yi * 668265263u));
    const uint8_t n10 = hash8_u32(seed ^ ((uint32_t)(xi + 1u) * 374761393u) ^ ((uint32_t)yi * 668265263u));
    const uint8_t n01 = hash8_u32(seed ^ ((uint32_t)xi * 374761393u) ^ ((uint32_t)(yi + 1u) * 668265263u));
    const uint8_t n11 = hash8_u32(seed ^ ((uint32_t)(xi + 1u) * 374761393u) ^ ((uint32_t)(yi + 1u) * 668265263u));

    const uint8_t nx0 = lerp_noise_u8(n00, n10, u);
    const uint8_t nx1 = lerp_noise_u8(n01, n11, u);
    return lerp_noise_u8(nx0, nx1, v);
}

/* ============================================================
 * Палитры (приближенные аналоги FastLED *Colors_p)
 * 16 ключевых цветов, интерполяция делается palette16_sample().
 * ============================================================ */

static const rgb8_t PAL_RAINBOW[16] = {
    {255,   0,   0}, {255,  64,   0}, {255, 128,   0}, {255, 192,   0},
    {255, 255,   0}, {128, 255,   0}, {  0, 255,   0}, {  0, 255, 128},
    {  0, 255, 255}, {  0, 128, 255}, {  0,   0, 255}, {128,   0, 255},
    {255,   0, 255}, {255,   0, 128}, {255,   0,  64}, {255,   0,   0},
};

static const rgb8_t PAL_CLOUD[16] = {
    {  0,   0,  32}, {  0,   0,  64}, {  0,  16,  96}, {  0,  32, 128},
    { 16,  64, 160}, { 32,  96, 192}, { 64, 128, 224}, { 96, 160, 255},
    {128, 192, 255}, {160, 224, 255}, {192, 240, 255}, {224, 248, 255},
    {255, 255, 255}, {224, 248, 255}, {160, 224, 255}, { 96, 160, 255},
};

static const rgb8_t PAL_LAVA[16] = {
    {  0,   0,   0}, { 32,   0,   0}, { 64,   0,   0}, { 96,   0,   0},
    {128,  16,   0}, {160,  32,   0}, {192,  48,   0}, {224,  64,   0},
    {255,  96,   0}, {255, 128,   0}, {255, 160,   0}, {255, 192,   0},
    {255, 224,   0}, {255, 255,  32}, {255, 255, 128}, {255, 255, 255},
};

static const rgb8_t PAL_FOREST[16] = {
    {  0,   8,   0}, {  0,  16,   0}, {  0,  32,   0}, {  0,  48,   0},
    {  0,  64,   0}, {  0,  80,   0}, {  0,  96,   0}, { 16, 112,   0},
    { 32, 128,   0}, { 48, 144,   0}, { 64, 160,  16}, { 80, 176,  32},
    { 96, 192,  48}, {112, 208,  64}, {128, 224,  80}, {160, 255, 128},
};

static const rgb8_t PAL_PLASMA[16] = {
    {  0,   0,   0}, { 16,   0,  32}, { 32,   0,  64}, { 64,   0,  96},
    { 96,   0, 128}, {128,   0, 160}, {160,   0, 192}, {192,   0, 224},
    {224,   0, 255}, {255,   0, 224}, {255,   0, 192}, {255,   0, 160},
    {255,   0, 128}, {255,  32,  96}, {255,  64,  64}, {255,  96,  32},
};

/* ============================================================
 * Общий рендер: “noise + palette”
 *
 * scale_88: шаг координаты noise на 1 пиксель (в 8.8)
 * x_speed_88/y_speed_88: скорость смещения (тоже в 8.8/сек)
 * seed_salt: уникальность “рисунка” на эффект
 * ============================================================ */
static void render_noise_palette(uint32_t t_ms,
                                 const rgb8_t pal[16],
                                 uint16_t scale_88,
                                 uint16_t x_speed_88,
                                 uint16_t y_speed_88,
                                 uint32_t seed_salt)
{
    /* Смещения считаем от времени.
     * Делим на 1000: скорость задаём “в 8.8 единицах в секунду”.
     */
    const uint16_t x_off = (uint16_t)((uint32_t)t_ms * (uint32_t)x_speed_88 / 1000u);
    const uint16_t y_off = (uint16_t)((uint32_t)t_ms * (uint32_t)y_speed_88 / 1000u);

    /* Соль + t_ms для мягкой “эволюции” */
    const uint32_t seed = (uint32_t)(seed_salt ^ ((uint32_t)t_ms * 2654435761u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t nx = (uint16_t)((x * scale_88) + x_off);
            const uint16_t ny = (uint16_t)((y * scale_88) + y_off);

            const uint8_t n = noise2d_88(nx, ny, seed);
            const rgb8_t c = palette16_sample(pal, n);

            matrix_ws2812_set_pixel_xy(x, y, c.r, c.g, c.b);
        }
    }
}

/* ============================================================
 * Портированные эффекты (Пакет 1)
 * ============================================================ */

/* 1) RAINBOW NOISE
 * Что делает:
 *   “радужные облака” с плавным дрейфом.
 */
void fx_rainbow_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);
    render_noise_palette(t, PAL_RAINBOW,
                         18u,  /* scale */
                         22u,  /* x drift */
                         11u,  /* y drift */
                         0xA1B2C3D4u);
}

/* 2) CLOUD NOISE
 * Что делает:
 *   холодные “облака/туман” в сине-белых тонах.
 */
void fx_cloud_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);
    render_noise_palette(t, PAL_CLOUD,
                         14u,
                         10u,
                         6u,
                         0x13579BDFu);
}

/* 3) LAVA NOISE
 * Что делает:
 *   “лава” с контрастными переходами к белым “вспышкам”.
 */
void fx_lava_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);
    render_noise_palette(t, PAL_LAVA,
                         16u,
                         18u,
                         4u,
                         0x0F00BA11u);
}

/* 4) FOREST NOISE
 * Что делает:
 *   зелёный “лесной” шум, похожий на перелив листвы/северное сияние в зелени.
 */
void fx_forest_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);
    render_noise_palette(t, PAL_FOREST,
                         15u,
                         8u,
                         12u,
                         0xCAFEBABEu);
}

/* 5) PLASMA NOISE
 * Что делает:
 *   фиолетово-розовая “плазма”, более динамичная по структуре.
 */
void fx_plasma_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);
    render_noise_palette(t, PAL_PLASMA,
                         20u,
                         26u,
                         15u,
                         0xDEADC0DEu);
}
