#include <stdint.h>
#include <stdbool.h>

#include "fx_engine.h"
#include "matrix_ws2812.h"

/* ============================================================
 * fx_effects_noise_2.c
 *
 * Назначение:
 *   Пакет 2 портированных эффектов из noiseEffects.ino:
 *     - OCEAN NOISE
 *     - PARTY NOISE
 *     - RAINBOW STRIPES (noise + полосы)
 *     - ZEBRA (контрастные полосы)
 *     - CONFETTI (частицы с “шлейфом” через локальный буфер)
 *
 * Примечания по архитектуре:
 *   - Все эффекты рисуют кадр полностью.
 *   - CONFETTI использует маленький статический буфер (RGB на каждый пиксель),
 *     чтобы получить затухание/след, аналогично fadeToBlackBy в FastLED.
 * ============================================================ */

typedef struct { uint8_t r, g, b; } rgb8_t;

/* ---------------- helpers: lerp/palette ---------------- */

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, uint8_t t)
{
    return (uint8_t)(a + (uint16_t)(b - a) * (uint16_t)t / 255u);
}

static inline rgb8_t lerp_rgb(rgb8_t a, rgb8_t b, uint8_t t)
{
    rgb8_t o = { lerp_u8(a.r, b.r, t), lerp_u8(a.g, b.g, t), lerp_u8(a.b, b.b, t) };
    return o;
}

static rgb8_t palette16_sample(const rgb8_t pal[16], uint8_t idx)
{
    const uint8_t seg  = (uint8_t)(idx >> 4);            /* 0..15 */
    const uint8_t frac = (uint8_t)((idx & 0x0Fu) * 17u); /* 0..255 */
    const rgb8_t a = pal[seg];
    const rgb8_t b = pal[(uint8_t)((seg + 1u) & 0x0Fu)];
    return lerp_rgb(a, b, frac);
}

/* ---------------- helpers: 2D value-noise (8.8) ---------------- */

static inline uint8_t hash8_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (uint8_t)(x >> 24);
}

static inline uint8_t fade_u8(uint8_t t)
{
    const uint16_t tt = (uint16_t)t * (uint16_t)t;
    const uint16_t t2 = (uint16_t)((3u * 255u) - (2u * t));
    const uint32_t v  = (uint32_t)tt * (uint32_t)t2;
    return (uint8_t)(v / (255u * 255u));
}

static inline uint8_t lerp_noise_u8(uint8_t a, uint8_t b, uint8_t t)
{
    return (uint8_t)(a + (uint16_t)(b - a) * (uint16_t)t / 255u);
}

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

/* ---------------- palettes (approx FastLED analogs) ---------------- */

static const rgb8_t PAL_OCEAN[16] = {
    {  0,  0, 16}, {  0,  0, 32}, {  0,  0, 64}, {  0, 16, 96},
    {  0, 32,128}, {  0, 64,160}, {  0, 96,192}, {  0,128,224},
    {  0,160,255}, {  0,192,224}, {  0,224,192}, {  0,255,160},
    { 32,255,192}, { 64,255,224}, {128,255,255}, {255,255,255},
};

static const rgb8_t PAL_PARTY[16] = {
    {255,  0,  0}, {255, 64,  0}, {255,128,  0}, {255,255,  0},
    {128,255,  0}, {  0,255,  0}, {  0,255,128}, {  0,255,255},
    {  0,128,255}, {  0,  0,255}, {128,  0,255}, {255,  0,255},
    {255,  0,128}, {255,  0, 64}, {255,255,255}, {  0,  0,  0},
};

static const rgb8_t PAL_RAINBOW[16] = {
    {255,   0,   0}, {255,  64,   0}, {255, 128,   0}, {255, 192,   0},
    {255, 255,   0}, {128, 255,   0}, {  0, 255,   0}, {  0, 255, 128},
    {  0, 255, 255}, {  0, 128, 255}, {  0,   0, 255}, {128,   0, 255},
    {255,   0, 255}, {255,   0, 128}, {255,   0,  64}, {255,   0,   0},
};

/* ---------------- generic render helpers ---------------- */

/* Общий “noise + palette” рендер */
static void render_noise_palette(uint32_t t_ms,
                                 const rgb8_t pal[16],
                                 uint16_t scale_88,
                                 uint16_t x_speed_88,
                                 uint16_t y_speed_88,
                                 uint32_t seed_salt)
{
    const uint16_t x_off = (uint16_t)((uint32_t)t_ms * (uint32_t)x_speed_88 / 1000u);
    const uint16_t y_off = (uint16_t)((uint32_t)t_ms * (uint32_t)y_speed_88 / 1000u);
    const uint32_t seed  = (uint32_t)(seed_salt ^ ((uint32_t)t_ms * 2654435761u));

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t nx = (uint16_t)((x * scale_88) + x_off);
            const uint16_t ny = (uint16_t)((y * scale_88) + y_off);
            const uint8_t  n  = noise2d_88(nx, ny, seed);
            const rgb8_t   c  = palette16_sample(pal, n);
            matrix_ws2812_set_pixel_xy(x, y, c.r, c.g, c.b);
        }
    }
}

/* ---------------- Effect #1: OCEAN NOISE ----------------
 * Визуально:
 *   “морская” текстура, с медленным потоком и глубиной.
 */
void fx_ocean_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);

    /* Немного разная скорость по X/Y даёт “течение” */
    render_noise_palette(t, PAL_OCEAN,
                         14u,  /* scale */
                         9u,   /* x drift */
                         5u,   /* y drift */
                         0x0CEAA11u);
}

/* ---------------- Effect #2: PARTY NOISE ----------------
 * Визуально:
 *   яркая “вечеринка”: быстрые цветовые пятна.
 */
void fx_party_noise_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);

    render_noise_palette(t, PAL_PARTY,
                         18u,
                         24u,
                         13u,
                         0xBAA7EEDu /* seed salt (валидный hex) */

                         );
}

/* ---------------- Effect #3: RAINBOW STRIPES ----------------
 * Визуально:
 *   “полосатая радуга”: noise + добавка полос по X.
 *
 * Трюк:
 *   берём noise и добавляем периодическую компоненту по X.
 */
void fx_rainbow_stripes_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);

    const uint16_t x_off = (uint16_t)((uint32_t)t * 20u / 1000u);
    const uint16_t y_off = (uint16_t)((uint32_t)t * 7u  / 1000u);
    const uint32_t seed  = (uint32_t)(0x5171F3u ^ (t * 2654435761u)); /* seed salt */

    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t nx = (uint16_t)((x * 16u) + x_off);
            const uint16_t ny = (uint16_t)((y * 16u) + y_off);

            uint8_t n = noise2d_88(nx, ny, seed);

            /* Полосы: добавка по X с периодом ~8 пикселей */
            const uint8_t stripe = (uint8_t)((x * 32u + (uint8_t)(t >> 4)) & 0xFFu);
            n = (uint8_t)(n + stripe);

            const rgb8_t c = palette16_sample(PAL_RAINBOW, n);
            matrix_ws2812_set_pixel_xy(x, y, c.r, c.g, c.b);
        }
    }
}

/* ---------------- Effect #4: ZEBRA ----------------
 * Визуально:
 *   контрастные “зебра-полосы”, которые “живут” от noise.
 *
 * Трюк:
 *   n -> порог/квантизация, получаем черно-белые (или почти) полосы.
 */
void fx_zebra_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    (void)ctx;

    /* Скорость тоже учитываем (чтобы режим не “мертвел” при низкой скорости) */
    const uint32_t t = (uint32_t)((uint64_t)t_ms * (uint64_t)ctx->speed_pct / 100u);

    const uint16_t x_off = (uint16_t)((uint32_t)t * 14u / 1000u);
    const uint16_t y_off = (uint16_t)((uint32_t)t * 9u  / 1000u);
    const uint32_t seed  = (uint32_t)(0x2E8FAu ^ (t * 2654435761u)); /* seed salt */


    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t nx = (uint16_t)((x * 20u) + x_off);
            const uint16_t ny = (uint16_t)((y * 20u) + y_off);

            uint8_t n = noise2d_88(nx, ny, seed);

            /* Усилим “полосатость” добавкой по X */
            n = (uint8_t)(n + (uint8_t)(x * 24u));

            /* Квантуем: 0 или 255 */
            const uint8_t v = (n & 0x80u) ? 255u : 0u;

            /* Чуть серого для “мягкости” (не чисто “сварка”) */
            const uint8_t vv = (uint8_t)(v ? 220u : 10u);
            matrix_ws2812_set_pixel_xy(x, y, vv, vv, vv);
        }
    }
}

/* ---------------- Effect #5: CONFETTI ----------------
 * Визуально:
 *   случайные цветные “конфетти” с затуханием следа.
 *
 * Важно:
 *   matrix_ws2812 не даёт читать текущий буфер, поэтому для затухания
 *   держим локальный RGB буфер (768*3 = 2304 байта для 48x16).
 */
static rgb8_t s_conf[MATRIX_LEDS_TOTAL];
static uint8_t s_conf_init = 0;

static inline uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline void conf_fade(uint8_t fade)
{
    /* fade: 0..255, где 255 = почти не гасим, 200 = заметное затухание */
    for (uint16_t i = 0; i < MATRIX_LEDS_TOTAL; i++) {
        s_conf[i].r = (uint8_t)((uint16_t)s_conf[i].r * (uint16_t)fade / 255u);
        s_conf[i].g = (uint8_t)((uint16_t)s_conf[i].g * (uint16_t)fade / 255u);
        s_conf[i].b = (uint8_t)((uint16_t)s_conf[i].b * (uint16_t)fade / 255u);
    }
}

void fx_confetti_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    if (!s_conf_init) {
        for (uint16_t i = 0; i < MATRIX_LEDS_TOTAL; i++) s_conf[i] = (rgb8_t){0,0,0};
        s_conf_init = 1;
    }

    /* Скорость управляет:
     *   - частотой появления частиц
     *   - скоростью “смыва” (затухания)
     */
    const uint32_t spd = (ctx->speed_pct < 10u) ? 10u : ctx->speed_pct;

    /* fade ближе к 255 = медленнее затухает (длиннее шлейф) */
    const uint8_t fade = (spd >= 200u) ? 210u : (uint8_t)(230u - (spd / 4u));
    conf_fade(fade);

    /* RNG seed от фазы/времени: детерминированно, но живо */
    uint32_t s = (uint32_t)(0xC0FF377u ^ (ctx->phase + t_ms * 17u)); /* seed salt */

    /* Сколько “конфетти” за кадр:
     *  - на низкой скорости: 1..2
     *  - на высокой: до ~6
     */
    uint8_t pops = 1u + (uint8_t)(spd / 60u);
    if (pops > 6u) pops = 6u;

    for (uint8_t k = 0; k < pops; k++) {
        const uint32_t r0 = xorshift32(&s);

        const uint16_t idx = (uint16_t)(r0 % (uint32_t)MATRIX_LEDS_TOTAL);
        const uint8_t  hue = (uint8_t)(r0 >> 16);

        const rgb8_t c = palette16_sample(PAL_RAINBOW, hue);

        /* “Добавляем” яркую частицу */
        s_conf[idx].r = (uint8_t)((uint16_t)s_conf[idx].r + (uint16_t)c.r > 255u ? 255u : (s_conf[idx].r + c.r));
        s_conf[idx].g = (uint8_t)((uint16_t)s_conf[idx].g + (uint16_t)c.g > 255u ? 255u : (s_conf[idx].g + c.g));
        s_conf[idx].b = (uint8_t)((uint16_t)s_conf[idx].b + (uint16_t)c.b > 255u ? 255u : (s_conf[idx].b + c.b));
    }

    /* Выводим буфер на матрицу */
    for (uint16_t y = 0; y < MATRIX_H; y++) {
        for (uint16_t x = 0; x < MATRIX_W; x++) {
            const uint16_t idx = matrix_ws2812_xy_to_index(x, y);
            const rgb8_t c = s_conf[idx];
            matrix_ws2812_set_pixel_xy(x, y, c.r, c.g, c.b);
        }
    }
}
