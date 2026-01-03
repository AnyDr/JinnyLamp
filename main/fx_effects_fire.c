// main/fx_effects_fire.c
#include "fx_engine.h"
#include "fx_canvas.h"

#include <stdint.h>
#include <stdbool.h>

#include "esp_random.h" // esp_random()

/* ============================================================
 * FIRE (new) — Jinny Lamp
 *  Logical sim: 16 (circumference) x 48 (height), ly=0 bottom
 *  Physical canvas: MATRIX_W x MATRIX_H (currently 48x16)
 *
 *  Features:
 *   - ignition (reset): bottom->top flash, then steady fire
 *   - steady tip height ~18..25, jets/flairs up to ~45
 *   - ragged top (no ring), per-column irregularity
 *   - detached "petals" rising above 45 and fading out, wind-coupled
 *   - base sparks (blue/cyan/green) every 1..3 s (points + tiny comets)
 *
 *  Brightness:
 *   - uses ctx->brightness (Variant A) + small floor for b=0 ("barely visible")
 *   - NOTE: matrix_ws2812_set_pixel_xy() already applies global s_bri scaling.
 * ============================================================ */

/* -------------------- Orientation / mapping -------------------- */
/* Do NOT change unless you re-wire physical panels.
 * We keep the proven mapping approach: logical 16x48 -> physical 48x16 by segments.
 */
#ifndef FIRE_STACK_REVERSE
#define FIRE_STACK_REVERSE 0   // 1 if height segments swapped
#endif

#ifndef FIRE_ROW_INVERT
#define FIRE_ROW_INVERT   0    // 1 if each 16x16 segment needs Y inversion
#endif

/* -------------------- Tunables (main knobs) -------------------- */
#define FIRE_W                 16
#define FIRE_H                 48

/* Speed handling:
 * we integrate in "steps" driven by t_ms delta and speed_pct.
 * Base: ~25 steps/sec at speed=100.
 */
#define FIRE_BASE_STEP_MS      40u   // 40ms -> 25 Hz sim at speed=100
#define FIRE_DT_CAP_MS         120u  // cap big frame gaps

/* Ignition (reset) */
#define FIRE_IGNITE_MS         650u  // ignition ramp duration
#define FIRE_IGNITE_BOOST      80    // extra injection during ignition

/* Target look */
#define FIRE_TIP_STEADY_MIN    10
#define FIRE_TIP_STEADY_MAX    42
#define FIRE_JET_TOP_Y         50    // jets allowed to reach this height
#define FIRE_PETAL_ABOVE_Y     45    // petals may rise above this

/* Field dynamics */
#define FIRE_BASE_INJECT       140   // base fuel injection (0..255)
#define FIRE_INJECT_NOISE      190    // per-column injection variance
#define FIRE_COOL_BASE         6     // base cooling per step
#define FIRE_COOL_Y_SLOPE      2     // extra cooling with height
#define FIRE_DIFFUSE           3    // 0..255, higher = more diffusion/smoothing
#define FIRE_UPFLOW            215   // 0..255, higher = stronger upward advection
#define FIRE_UPFLOW_JET_BOOST  140    // extra upflow during jets (local)

/* Wind */
#define FIRE_WIND_JITTER_Q8    250    // small constant jitter amplitude (q8)
#define FIRE_WIND_GUST_Q8      220   // gust amplitude (q8), if enabled
#define FIRE_WIND_GUST_ENABLE  1     // 0 = only micro-jitter, 1 = add rare gusts
#define FIRE_WIND_RESP         18    // 0..255, response speed to target

/* "Tongues" (emitters) */
#define FIRE_TONGUES           4
#define FIRE_TONGUE_W_MIN      2
#define FIRE_TONGUE_W_MAX      4
#define FIRE_TONGUE_PWR_MIN    90
#define FIRE_TONGUE_PWR_MAX    170
#define FIRE_TONGUE_DRIFT_Q8   22    // horizontal drift speed

/* Hot islands (yellow inside tongues) */
#define FIRE_ISLANDS_ENABLE    1
#define FIRE_ISLANDS_RATE      24    // lower -> more frequent
#define FIRE_ISLANDS_Y_MIN     6
#define FIRE_ISLANDS_Y_MAX     15
#define FIRE_ISLANDS_PWR       120   // add-heat amount

/* Jets / flares */
#define FIRE_JETS_ENABLE       1
#define FIRE_JET_RATE          22    // lower -> more frequent jets
#define FIRE_JET_LIFE_STEPS    22    // duration in sim steps
#define FIRE_JET_PWR           255   // injection strength during jet
#define FIRE_JET_WIDTH_MIN     2
#define FIRE_JET_WIDTH_MAX     4

/* Petals (detached fragments from ragged edge) */
#define FIRE_PETALS_ENABLE     1
#define FIRE_PETALS_MAX        5
#define FIRE_PETAL_AREA_MIN    3
#define FIRE_PETAL_AREA_MAX    9
#define FIRE_PETAL_RATE        15    // lower -> more petals
#define FIRE_PETAL_LIFE_MIN    140
#define FIRE_PETAL_LIFE_MAX    260
#define FIRE_PETAL_VY_Q8       210   // upward velocity
#define FIRE_PETAL_VX_Q8       120    // wind coupling factor (q8)
/* DEBUG: temporary color split to distinguish petals vs sparks */
#define FIRE_DEBUG_COLOR_SPLIT  1   // 1=override colors, 0=normal palette

#define FIRE_DEBUG_PETAL_R      255
#define FIRE_DEBUG_PETAL_G      110
#define FIRE_DEBUG_PETAL_B      0

#define FIRE_DEBUG_SPARK_R      80
#define FIRE_DEBUG_SPARK_G      0
#define FIRE_DEBUG_SPARK_B      255


/* Sparks (base, 1..3 seconds, points + tiny comets) */
#define FIRE_SPARKS_ENABLE     1
#define FIRE_SPARKS_MAX        12
#define FIRE_SPARK_MIN_MS      2500u
#define FIRE_SPARK_MAX_MS      8000u
#define FIRE_SPARK_LIFE_MIN    30
#define FIRE_SPARK_LIFE_MAX    75
#define FIRE_SPARK_VY_MIN_Q8   140   // было внутри кода ~140
#define FIRE_SPARK_VY_MAX_Q8   260   // было ~260

/* --- TIP PROFILE (ragged edge memory + amplitude) --- */
#define FIRE_TIP_PROFILE_ENABLE   1

/* Амплитуда профиля относительно текущей кромки.
 * Q8: 256 = 1.0 (как сейчас), 384 = 1.5x, 512 = 2.0x
 */
#define FIRE_TIP_AMP_Q8           600 // Размах по вертикали (главная ручка)

/* Инерция профиля: раздельно подъём и спад (в процентах "дельты за кадр").
 * Меньше = более вязко/дольше живёт.
 * Рекомендую: rise быстрее, fall медленнее.
 */
#define FIRE_TIP_RISE_Q8          80    // 0..255 как быстро “нарастает” вверх
#define FIRE_TIP_FALL_Q8          10    // 0..255 как быстро “умирает/оседает” (чем ниже цифра - тем медленнее)

/* С какой высоты начинаем искажать выборку (чтобы не трогать "тело" огня) */
#define FIRE_TIP_APPLY_Y          14    // 0..47

/* Ограничение максимального вертикального сдвига выборки (пиксели) */
#define FIRE_TIP_MAX_SHIFT_PX     28


/* Brightness variant A: ctx->brightness with floor for 0 */
#define FIRE_BRI_FLOOR0        12    // ctx->brightness==0 -> use this (barely visible)
#define FIRE_BRI_MAX_CLAMP     255   // keep 255 unless you need headroom

/* -------------------- Helpers -------------------- */
static inline uint8_t u8_clamp_i32(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline uint8_t u8_lerp(uint8_t a, uint8_t b, uint8_t t)
{
    return (uint8_t)(a + (int)(b - a) * (int)t / 255);
}

static inline uint8_t scale_u8(uint8_t v, uint8_t scale)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)scale + 127u) / 255u);
}

static inline uint32_t rnd_u32(void)
{
    /* esp_random() already decent. */
    return (uint32_t)esp_random();
}

static inline uint8_t rnd_u8(void)
{
    return (uint8_t)(rnd_u32() >> 24);
}

static inline uint8_t hash8_u16(uint16_t s)
{
    /* very small hash, deterministic */
    s ^= (uint16_t)(s << 7);
    s ^= (uint16_t)(s >> 9);
    s ^= (uint16_t)(s << 8);
    return (uint8_t)(s & 0xFF);
}


static inline int wrap_x(int x)
{
    while (x < 0) x += FIRE_W;
    while (x >= FIRE_W) x -= FIRE_W;
    return x;
}

static inline uint8_t heat_get(const uint8_t h[FIRE_H][FIRE_W], int x, int y)
{
    if (y < 0) y = 0;
    if (y >= FIRE_H) y = FIRE_H - 1;
    x = wrap_x(x);
    return h[y][x];
}

/* Map logical (lx:0..15, ly:0..47, ly=0 bottom) -> physical canvas (x:0..47, y:0..15) */
static inline void map_to_canvas(int lx, int ly, uint16_t *cx, uint16_t *cy)
{
    int seg = ly / 16;   // 0 bottom, 1 mid, 2 top
    int row = ly % 16;   // 0..15 inside segment (0 is bottom in logical)

#if FIRE_STACK_REVERSE
    seg = 2 - seg;
#endif

    int x = seg * 16 + lx; // 0..47
    int y = row;           // 0..15

#if FIRE_ROW_INVERT
    y = 15 - y;
#endif

    *cx = (uint16_t)x;
    *cy = (uint16_t)y;
}

/* -------------------- Color palette (warm fire, no cold white) -------------------- */
static void heat_to_rgb(uint8_t heat, uint8_t bri, uint8_t *r, uint8_t *g, uint8_t *b)
{
    /* heat 0..255 -> warm ramp:
     *  - low: dark red
     *  - mid: red->orange
     *  - high: orange->yellow->yellow-white (but stays warm)
     */
    int rr = 0, gg = 0, bb = 0;

    if (heat < 48) {
        rr = heat * 3;          // 0..144
        gg = heat / 4;          // 0..12
        bb = 0;
    } else if (heat < 140) {
        int t = (int)heat - 48; // 0..91
        rr = 144 + t * 2;       // 144..326
        gg = 12  + t * 2;       // 12..194
        bb = 0;
    } else {
        int t = (int)heat - 140; // 0..115
        rr = 255;
        gg = 194 + (t * 1);      // 194..309 (clamp later)
        bb = (t > 70) ? (t - 70) / 6 : 0; // tiny warm lift, never "blue"
    }

    rr = (rr > 255) ? 255 : rr;
    gg = (gg > 255) ? 255 : gg;
    bb = (bb > 22)  ? 22  : bb; // protect from "white/blue"

    uint8_t R = (uint8_t)rr;
    uint8_t G = (uint8_t)gg;
    uint8_t B = (uint8_t)bb;

    /* brightness */
    R = scale_u8(R, bri);
    G = scale_u8(G, bri);
    B = scale_u8(B, bri);

    *r = R; *g = G; *b = B;
}

/* -------------------- State -------------------- */
typedef struct {
    int16_t x_q8;
    int16_t vx_q8;
    uint8_t w;         // width (cells)
    uint8_t pwr;       // power
    uint16_t next_ms;  // retarget timer
} tongue_t;

typedef struct {
    bool    alive;
    int16_t x_q8;
    int16_t y_q8;
    int16_t vx_q8;
    int16_t vy_q8;
    uint8_t heat;
    uint8_t life;      // steps
    uint8_t size;      // 0..2 => 3px / 5px / 9px blob
    uint8_t area;      // target area in pixels (3..15)
    uint16_t seed;     // per-petal shape seed

} petal_t;

typedef struct {
    bool    alive;
    int16_t x_q8;
    int16_t y_q8;
    int16_t vx_q8;
    int16_t vy_q8;
    uint8_t r, g, b;
    uint8_t life;      // steps
    uint8_t tail;      // 0 point, 1..2 tail length
} spark_t;

/* Field buffers */
static uint8_t s_heat[FIRE_H][FIRE_W];
static uint8_t s_tmp [FIRE_H][FIRE_W];

/* Persistent column "fuel personality" to avoid ring look */
static uint8_t s_fuel_bias[FIRE_W];

/* Wind */
static int16_t s_wind_q8 = 0;
static int16_t s_wind_tgt_q8 = 0;
static uint16_t s_wind_timer_ms = 0;
static uint16_t s_gust_timer_ms = 0;

/* Tongues */
static tongue_t s_tong[FIRE_TONGUES];

/* Jets */
static int16_t s_jet_x_q8 = 0;
static uint8_t s_jet_w = 3;
static int16_t s_jet_life = 0;

/* Petals + sparks */
static petal_t s_pet[FIRE_PETALS_MAX];
static spark_t s_spk[FIRE_SPARKS_MAX];
static uint32_t s_next_spark_ms = 0;

#if FIRE_TIP_PROFILE_ENABLE
static bool    s_tip_init = false;
static int16_t s_tip_filt_q8[FIRE_W];     /* filtered tip (Q8) */
static int16_t s_tip_delta_q8[FIRE_W];    /* amplified delta vs mean (Q8, signed) */
static int8_t  s_tip_delta_px[FIRE_W];    /* delta in pixels (signed), clamped */
#endif


/* Timing / init */
static bool     s_inited = false;
static uint32_t s_last_ms = 0;
static uint32_t s_accum_ms = 0;
static uint32_t s_ignite_ms = 0;
static uint32_t s_last_seen_frame = 0;

/* -------------------- Reset / init -------------------- */
static void fire_reset(uint32_t t_ms)
{
    for (int y = 0; y < FIRE_H; y++) {
        for (int x = 0; x < FIRE_W; x++) {
            s_heat[y][x] = 0;
            s_tmp[y][x]  = 0;
        }
    }

    for (int x = 0; x < FIRE_W; x++) {
        /* bias: wide enough to break "ring" symmetry */
        s_fuel_bias[x] = (uint8_t)(40 + (rnd_u8() % 160)); // 40..199
    }

    s_wind_q8 = 0;
    s_wind_tgt_q8 = 0;
    s_wind_timer_ms = 0;
    s_gust_timer_ms = 0;

    for (int i = 0; i < FIRE_TONGUES; i++) {
        s_tong[i].x_q8 = (int16_t)(((i * (256 * FIRE_W)) / FIRE_TONGUES) + (rnd_u8() & 0x7F));
        s_tong[i].vx_q8 = (int16_t)((int)(rnd_u8() % (FIRE_TONGUE_DRIFT_Q8 * 2 + 1)) - FIRE_TONGUE_DRIFT_Q8);
        s_tong[i].w   = (uint8_t)(FIRE_TONGUE_W_MIN + (rnd_u8() % (FIRE_TONGUE_W_MAX - FIRE_TONGUE_W_MIN + 1)));
        s_tong[i].pwr = (uint8_t)(FIRE_TONGUE_PWR_MIN + (rnd_u8() % (FIRE_TONGUE_PWR_MAX - FIRE_TONGUE_PWR_MIN + 1)));
        s_tong[i].next_ms = (uint16_t)(250 + (rnd_u8() % 900)); // retarget in 0.25..1.15s
    }

    s_jet_x_q8 = (int16_t)((rnd_u8() % FIRE_W) * 256);
    s_jet_w = (uint8_t)(FIRE_JET_WIDTH_MIN + (rnd_u8() % (FIRE_JET_WIDTH_MAX - FIRE_JET_WIDTH_MIN + 1)));
    s_jet_life = 0;

    for (int i = 0; i < FIRE_PETALS_MAX; i++) s_pet[i].alive = false;
    for (int i = 0; i < FIRE_SPARKS_MAX; i++) s_spk[i].alive = false;

    s_next_spark_ms = t_ms + FIRE_SPARK_MIN_MS + (rnd_u32() % (FIRE_SPARK_MAX_MS - FIRE_SPARK_MIN_MS + 1u));

    s_last_ms = t_ms;
    s_accum_ms = 0;
    s_ignite_ms = 0;
    #if FIRE_TIP_PROFILE_ENABLE
    s_tip_init = false;
    #endif

    s_inited = true;
}

/* -------------------- Wind update -------------------- */
static void fire_wind_update(uint32_t dt_ms)
{
    /* small jitter target every ~80..180ms */
    if (s_wind_timer_ms <= dt_ms) {
        s_wind_timer_ms = (uint16_t)(80 + (rnd_u8() % 100));
        int16_t jitter = (int16_t)((int)(rnd_u8() % (FIRE_WIND_JITTER_Q8 * 2 + 1)) - FIRE_WIND_JITTER_Q8);
        s_wind_tgt_q8 = jitter;
    } else {
        s_wind_timer_ms = (uint16_t)(s_wind_timer_ms - dt_ms);
    }

#if FIRE_WIND_GUST_ENABLE
    /* rare gust: 1.2..3.0s */
    if (s_gust_timer_ms <= dt_ms) {
        s_gust_timer_ms = (uint16_t)(1200 + (rnd_u8() % 1800));
        int16_t gust = (int16_t)((int)(rnd_u8() % (FIRE_WIND_GUST_Q8 * 2 + 1)) - FIRE_WIND_GUST_Q8);
        /* combine with jitter */
        s_wind_tgt_q8 = (int16_t)(s_wind_tgt_q8 + gust);
    } else {
        s_gust_timer_ms = (uint16_t)(s_gust_timer_ms - dt_ms);
    }
#endif

    /* smooth response */
    int16_t err = (int16_t)(s_wind_tgt_q8 - s_wind_q8);
    s_wind_q8 = (int16_t)(s_wind_q8 + (err * (int)FIRE_WIND_RESP) / 255);
}

/* -------------------- Tongues update -------------------- */
static void fire_tongues_update(uint32_t dt_ms)
{
    for (int i = 0; i < FIRE_TONGUES; i++) {
        tongue_t *t = &s_tong[i];

        /* drift */
        t->x_q8 = (int16_t)(t->x_q8 + t->vx_q8);
        /* wrap in q8 */
        int32_t x = t->x_q8;
        while (x < 0) x += (256 * FIRE_W);
        while (x >= (256 * FIRE_W)) x -= (256 * FIRE_W);
        t->x_q8 = (int16_t)x;

        /* retarget occasionally */
        if (t->next_ms <= dt_ms) {
            t->next_ms = (uint16_t)(260 + (rnd_u8() % 1100));

            /* change width/power a bit */
            t->w = (uint8_t)(FIRE_TONGUE_W_MIN + (rnd_u8() % (FIRE_TONGUE_W_MAX - FIRE_TONGUE_W_MIN + 1)));
            t->pwr = (uint8_t)(FIRE_TONGUE_PWR_MIN + (rnd_u8() % (FIRE_TONGUE_PWR_MAX - FIRE_TONGUE_PWR_MIN + 1)));

            /* new drift (small) */
            t->vx_q8 = (int16_t)((int)(rnd_u8() % (FIRE_TONGUE_DRIFT_Q8 * 2 + 1)) - FIRE_TONGUE_DRIFT_Q8);
        } else {
            t->next_ms = (uint16_t)(t->next_ms - dt_ms);
        }
    }
}

/* -------------------- Jets update -------------------- */
static void fire_jets_update(void)
{
#if FIRE_JETS_ENABLE
    if (s_jet_life > 0) {
        s_jet_life--;
        return;
    }

    /* chance to start a jet */
    if ((rnd_u8() % FIRE_JET_RATE) == 0) {
        s_jet_x_q8 = (int16_t)((rnd_u8() % FIRE_W) * 256);
        s_jet_w = (uint8_t)(FIRE_JET_WIDTH_MIN + (rnd_u8() % (FIRE_JET_WIDTH_MAX - FIRE_JET_WIDTH_MIN + 1)));
        s_jet_life = FIRE_JET_LIFE_STEPS;
    }
#else
    (void)0;
#endif
}

/* -------------------- Injection at base -------------------- */
static void fire_inject(uint8_t ignite_k /*0..255*/)
{
    /* inject into y=0..2 */
    for (int x = 0; x < FIRE_W; x++) {
        int inj = FIRE_BASE_INJECT;

        /* per-column personality breaks ring symmetry */
        inj += ((int)s_fuel_bias[x] - 120); // -80..+79 approx

        /* additional random per-step noise */
        inj += (int)(rnd_u8() % (FIRE_INJECT_NOISE + 1)) - (FIRE_INJECT_NOISE / 2);

        /* tongues contributions */
        for (int i = 0; i < FIRE_TONGUES; i++) {
            const tongue_t *t = &s_tong[i];
            int tx = (int)(t->x_q8 >> 8); // 0..15
            int dx = x - tx;
            /* circular distance */
            if (dx < 0) dx = -dx;
            int d2 = FIRE_W - dx;
            if (d2 < dx) dx = d2;

            /* simple bell falloff by dx */
            int w = (int)t->w;
            if (dx <= w) {
                int k = (w - dx + 1);     // 1..w+1
                inj += (t->pwr * k) / (w + 1);
            }
        }

#if FIRE_JETS_ENABLE
        /* if jet active and x near jet center, boost injection */
        if (s_jet_life > 0) {
            int jx = (int)(s_jet_x_q8 >> 8);
            int dx = x - jx;
            if (dx < 0) dx = -dx;
            int d2 = FIRE_W - dx;
            if (d2 < dx) dx = d2;

            if (dx <= (int)s_jet_w) {
                int k = ((int)s_jet_w - dx + 1);
                inj += (FIRE_JET_PWR * k) / ((int)s_jet_w + 1);
            }
        }
#endif

        /* ignition ramp */
        inj += (FIRE_IGNITE_BOOST * (int)ignite_k) / 255;

        inj = (inj < 0) ? 0 : inj;
        inj = (inj > 255) ? 255 : inj;

        /* write to bottom rows with slight vertical gradient */
        int v0 = inj;
        int v1 = (inj * 3) / 4;
        int v2 = (inj * 1) / 2;

        s_heat[0][x] = u8_clamp_i32((int)s_heat[0][x] + v0);
        s_heat[1][x] = u8_clamp_i32((int)s_heat[1][x] + v1);
        s_heat[2][x] = u8_clamp_i32((int)s_heat[2][x] + v2);
    }
}

/* -------------------- Hot islands (yellow pockets) -------------------- */
static void fire_islands(void)
{
#if FIRE_ISLANDS_ENABLE
    if ((rnd_u8() % FIRE_ISLANDS_RATE) != 0) return;

    int x0 = (int)(rnd_u8() % FIRE_W);
    int y0 = FIRE_ISLANDS_Y_MIN + (int)(rnd_u8() % (FIRE_ISLANDS_Y_MAX - FIRE_ISLANDS_Y_MIN + 1));
    int rad = 1 + (rnd_u8() & 1);

    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            int y = y0 + dy;
            int x = wrap_x(x0 + dx);
            if (y < 0 || y >= FIRE_H) continue;

            int add = FIRE_ISLANDS_PWR - (dx*dx + dy*dy) * 30;
            if (add <= 0) continue;
            s_heat[y][x] = u8_clamp_i32((int)s_heat[y][x] + add);
        }
    }
#endif
}

/* -------------------- Field step (advection + diffusion + cooling) -------------------- */
static void fire_step_field(uint8_t upflow, uint8_t diffuse, uint8_t cool_base, uint8_t cool_slope, int16_t wind_q8)
{
    /* For each cell y>=1, advect from y-1 with small wind shift.
     * Use Q8 wind: shift = (wind_q8 * y) / (something) for slightly more sway near top.
     */
    for (int y = FIRE_H - 1; y >= 1; y--) {
        /* wind shift fraction */
        int16_t wy_q8 = (int16_t)((wind_q8 * (int16_t)(y + 6)) / 20); // stronger with height
        int x_shift = (int)(wy_q8 >> 8);
        uint8_t frac = (uint8_t)(wy_q8 & 0xFF);

        for (int x = 0; x < FIRE_W; x++) {
            /* source coords in previous row */
            int sx0 = wrap_x(x - x_shift);
            int sx1 = wrap_x(sx0 - ((wy_q8 >= 0) ? 1 : -1));

            uint8_t a = s_heat[y - 1][sx0];
            uint8_t b = s_heat[y - 1][sx1];

            /* interpolate horizontally */
            uint8_t src = (uint8_t)(((uint16_t)a * (uint16_t)(255 - frac) + (uint16_t)b * (uint16_t)frac) / 255u);

            /* apply upflow (mix with existing heat for stability) */
            uint8_t cur = s_heat[y][x];
            uint8_t adv = u8_lerp(cur, src, upflow);

            /* diffusion (neighbor average) */
            uint8_t n0 = heat_get(s_heat, x, y);
            uint8_t n1 = heat_get(s_heat, x - 1, y);
            uint8_t n2 = heat_get(s_heat, x + 1, y);
            uint8_t n3 = heat_get(s_heat, x, y - 1);
            uint8_t n4 = heat_get(s_heat, x, y + 1);
            uint8_t avg = (uint8_t)((n0 + n1 + n2 + n3 + n4) / 5);

            uint8_t diff = u8_lerp(adv, avg, diffuse);

            /* cooling by height */
            int cool = (int)cool_base + ((int)cool_slope * y) / 8;
            cool += (int)(rnd_u8() & 1); // tiny stochastic to avoid banding
            int v = (int)diff - cool;
            if (v < 0) v = 0;

            s_tmp[y][x] = (uint8_t)v;
        }
    }

    /* bottom row cooling (keep alive but not over-saturate) */
    for (int x = 0; x < FIRE_W; x++) {
        int v = (int)s_heat[0][x] - (int)(cool_base / 2);
        if (v < 0) v = 0;
        s_tmp[0][x] = (uint8_t)v;
    }

    /* swap tmp -> heat */
    for (int y = 0; y < FIRE_H; y++) {
        for (int x = 0; x < FIRE_W; x++) {
            s_heat[y][x] = s_tmp[y][x];
        }
    }
}

/* -------------------- Tip height estimation (for ragged edge & petals) -------------------- */
static int fire_tip_y_of_col(int x)
{
    /* find highest y where heat above threshold */
    const uint8_t thr = 64;
    for (int y = FIRE_H - 1; y >= 0; y--) {
        if (s_heat[y][x] >= thr) return y;
    }
    return 0;
}

#if FIRE_TIP_PROFILE_ENABLE
static void fire_tip_profile_update_q8(const int16_t tip_raw_q8[FIRE_W])
{
    if (!s_tip_init) {
        for (int x = 0; x < FIRE_W; x++) {
            s_tip_filt_q8[x]  = tip_raw_q8[x];
            s_tip_delta_q8[x] = 0;
            s_tip_delta_px[x] = 0;
        }
        s_tip_init = true;
        return;
    }

    /* 1) filter tips with separate rise/fall */
    for (int x = 0; x < FIRE_W; x++) {
        int16_t cur = s_tip_filt_q8[x];
        int16_t raw = tip_raw_q8[x];
        int16_t d   = (int16_t)(raw - cur);

        uint8_t a = (d >= 0) ? (uint8_t)FIRE_TIP_RISE_Q8 : (uint8_t)FIRE_TIP_FALL_Q8;
        cur = (int16_t)(cur + (int16_t)(((int32_t)a * (int32_t)d) >> 8)); /* Q8 */
        s_tip_filt_q8[x] = cur;
    }

    /* 2) mean */
    int32_t sum = 0;
    for (int x = 0; x < FIRE_W; x++) sum += s_tip_filt_q8[x];
    int16_t mean = (int16_t)(sum / FIRE_W);

    /* 3) amplify around mean -> delta */
    for (int x = 0; x < FIRE_W; x++) {
        int16_t rel = (int16_t)(s_tip_filt_q8[x] - mean);          /* Q8 signed */
        int16_t rel_amp = (int16_t)(((int32_t)FIRE_TIP_AMP_Q8 * (int32_t)rel) >> 8);
        int16_t delta_q8 = (int16_t)(rel_amp - rel);

        s_tip_delta_q8[x] = delta_q8;

        int32_t dp = (int32_t)delta_q8 >> 8; /* px */
        if (dp >  FIRE_TIP_MAX_SHIFT_PX) dp =  FIRE_TIP_MAX_SHIFT_PX;
        if (dp < -FIRE_TIP_MAX_SHIFT_PX) dp = -FIRE_TIP_MAX_SHIFT_PX;
        s_tip_delta_px[x] = (int8_t)dp;
    }
}
#endif


/* -------------------- Petals -------------------- */
static void petals_spawn_from_tip(int x, int tip_y, int16_t wind_q8)
{
#if FIRE_PETALS_ENABLE
    if ((rnd_u8() % FIRE_PETAL_RATE) != 0) return;

    /* spawn slightly above tip, only if tip is in active range */
    if (tip_y < FIRE_TIP_STEADY_MIN - 2) return;

    /* find free slot */
    for (int i = 0; i < FIRE_PETALS_MAX; i++) {
        petal_t *p = &s_pet[i];
        if (p->alive) continue;

        p->alive = true;
        p->x_q8 = (int16_t)(x * 256 + (int16_t)((int)(rnd_u8() & 0x7F) - 64));
        p->y_q8 = (int16_t)(tip_y * 256 + (int16_t)(120 + (rnd_u8() & 0x3F)));

        /* upward + wind-coupled lateral */
        int16_t vx = (int16_t)((wind_q8 * FIRE_PETAL_VX_Q8) / 255);
        vx += (int16_t)((int)(rnd_u8() % 81) - 40);

        p->vx_q8 = vx;
        p->vy_q8 = (int16_t)(FIRE_PETAL_VY_Q8 + (int16_t)(rnd_u8() % 90));

        /* heat from local tip */
        uint8_t h = heat_get(s_heat, x, tip_y);
        uint8_t base = (h < 160) ? 160 : h;
        p->heat = (uint8_t)u8_clamp_i32((int)base + 30 + (rnd_u8() % 50)); // 190..255


        p->life = (uint8_t)(FIRE_PETAL_LIFE_MIN + (rnd_u8() % (FIRE_PETAL_LIFE_MAX - FIRE_PETAL_LIFE_MIN + 1)));
        p->size = (uint8_t)(rnd_u8() % 3); // 0..2
        p->seed = (uint16_t)rnd_u32();
        p->area = (uint8_t)(FIRE_PETAL_AREA_MIN +
        (rnd_u8() % (FIRE_PETAL_AREA_MAX - FIRE_PETAL_AREA_MIN + 1)));


        return;
    }
#endif
}

static void petals_step_and_render(uint8_t bri, int16_t wind_q8)
{
#if FIRE_PETALS_ENABLE

    /* local helper: additive draw logical pixel */
    #define ADD_LPX(_lx, _ly, _r, _g, _b, _k) do {                  \
        int __ly = (_ly);                                          \
        if (__ly < 0 || __ly >= FIRE_H) break;                     \
        int __lx = wrap_x((_lx));                                  \
        uint16_t __cx, __cy;                                       \
        map_to_canvas(__lx, __ly, &__cx, &__cy);                    \
        uint8_t __ar = scale_u8((_r), (_k));                       \
        uint8_t __ag = scale_u8((_g), (_k));                       \
        uint8_t __ab = scale_u8((_b), (_k));                       \
        uint8_t __cr, __cg, __cb;                                  \
        (void)fx_canvas_get(__cx, __cy, &__cr, &__cg, &__cb);       \
        __cr = u8_clamp_i32((int)__cr + (int)__ar);                \
        __cg = u8_clamp_i32((int)__cg + (int)__ag);                \
        __cb = u8_clamp_i32((int)__cb + (int)__ab);                \
        fx_canvas_set(__cx, __cy, __cr, __cg, __cb);               \
    } while (0)

    /* small deterministic hash (no extra state needed) */
    #define HASH8(_v) (uint8_t)((uint16_t)(_v) * 73u ^ ((uint16_t)(_v) >> 7) ^ ((uint16_t)(_v) << 5))

    /* candidate offsets around center (dy negative means upward) */
    static const int8_t off[][2] = {
        { 0,  0},
        { 0, -1}, { 0, -2}, { 0, -3}, { 0, -4},
        {-1,  0}, { 1,  0},
        {-1, -1}, { 1, -1},
        {-1, -2}, { 1, -2},
        {-2, -1}, { 2, -1},
        {-2, -2}, { 2, -2},
        {-1, -3}, { 1, -3},
        {-2, -3}, { 2, -3},
        {-3, -2}, { 3, -2},
        {-3, -3}, { 3, -3},
    };
    const int off_n = (int)(sizeof(off) / sizeof(off[0]));

    for (int i = 0; i < FIRE_PETALS_MAX; i++) {
        petal_t *p = &s_pet[i];
        if (!p->alive) continue;

        if (p->life == 0) { p->alive = false; continue; }
        p->life--;

        /* wind influence (small) */
        p->vx_q8 = (int16_t)(p->vx_q8 + (wind_q8 / 40));

        p->x_q8 = (int16_t)(p->x_q8 + p->vx_q8);
        p->y_q8 = (int16_t)(p->y_q8 + p->vy_q8);

        int x = (int)(p->x_q8 >> 8);
        int y = (int)(p->y_q8 >> 8);

        x = wrap_x(x);
        if (y >= FIRE_H) { p->alive = false; continue; }
        if (y < 0)       { p->alive = false; continue; }

        /* choose petal color */
        uint8_t r, g, b;
#if FIRE_DEBUG_COLOR_SPLIT
        r = scale_u8(FIRE_DEBUG_PETAL_R, bri);
        g = scale_u8(FIRE_DEBUG_PETAL_G, bri);
        b = scale_u8(FIRE_DEBUG_PETAL_B, bri);
#else
        uint8_t ph = p->heat;
        heat_to_rgb(ph, bri, &r, &g, &b);
        g = u8_clamp_i32((int)g + 18);
        b = (uint8_t)((b > 6) ? 6 : b);
#endif

        /* area target: 3..15 (derived from current state, no new fields needed) */
        uint16_t seed = (uint16_t)((uint16_t)p->heat << 8) ^ (uint16_t)(p->life * 29u) ^
                        (uint16_t)(x * 17u) ^ (uint16_t)(y * 43u) ^ (uint16_t)(wind_q8 >> 3);
        uint8_t h0 = HASH8(seed);
        int want = 3 + (h0 % 13); /* 3..15 */

        /* wind deformation bucket: -4..+4 */
        int wind_bucket = (int)(wind_q8 / 64);
        if (wind_bucket >  4) wind_bucket =  4;
        if (wind_bucket < -4) wind_bucket = -4;

        /* fade near top so it disappears naturally */
        uint8_t fade = 255;
        if (y > 40) {
            int d = y - 40;                 /* 1..7 */
            int f = 255 - d * 28;           /* ~255..59 */
            if (f < 40) f = 40;
            fade = (uint8_t)f;
        }

        int drawn = 0;

        /* pick irregular points from candidate set */
        for (int pass = 0; pass < 3 && drawn < want; pass++) {
            uint8_t thr = (uint8_t)(170 + pass * 30); /* acceptance: 155..215 => denser */
            for (int j = 0; j < off_n && drawn < want; j++) {
                uint16_t s = (uint16_t)(seed ^ (uint16_t)(j * 73u) ^ (uint16_t)(pass * 191u) ^
                                        (uint16_t)(wind_bucket * 97) ^ (uint16_t)(p->life * 11u));
                uint8_t hh = HASH8(s);
                if (hh > thr) continue;

                int dx = (int)off[j][0];
                int dy = (int)off[j][1];

                /* wind-deform: lean upper pixels more */
                int lean = (wind_bucket * (-dy)) / 2; /* dy negative => stronger lean upwards */
                dx += lean;

                /* intensity by Manhattan distance */
                int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                uint8_t k = 255;
                if (dist >= 6) k = 55;
                else if (dist == 5) k = 75;
                else if (dist == 4) k = 95;
                else if (dist == 3) k = 125;
                else if (dist == 2) k = 165;
                else if (dist == 1) k = 210;

                k = scale_u8(k, fade);
                ADD_LPX(x + dx, y + dy, r, g, b, k);
                drawn++;
            }
        }

        /* short tail (always) */
        //ADD_LPX(x, y - 5, r, g, b, scale_u8(70, fade)); temporary disabled the petal lighttails
        //ADD_LPX(x, y - 6, r, g, b, scale_u8(45, fade));
    }

    #undef ADD_LPX
    #undef HASH8

#else
    (void)bri; (void)wind_q8;
#endif
}


/* -------------------- Sparks -------------------- */
static void sparks_spawn(uint32_t t_ms)
{
#if FIRE_SPARKS_ENABLE
    if (t_ms < s_next_spark_ms) return;

    s_next_spark_ms = t_ms + FIRE_SPARK_MIN_MS + (rnd_u32() % (FIRE_SPARK_MAX_MS - FIRE_SPARK_MIN_MS + 1u));

    for (int i = 0; i < FIRE_SPARKS_MAX; i++) {
        spark_t *s = &s_spk[i];
        if (s->alive) continue;

        s->alive = true;
        int x = (int)(rnd_u8() % FIRE_W);
        s->x_q8 = (int16_t)(x * 256 + (int16_t)((int)(rnd_u8() & 0x7F) - 64));
        s->y_q8 = (int16_t)((int)(rnd_u8() % 3) * 256);

        /* upward and a little sideways */
        s->vy_q8 = (int16_t)(
            FIRE_SPARK_VY_MIN_Q8 +
            (int16_t)(rnd_u8() % (uint8_t)(FIRE_SPARK_VY_MAX_Q8 - FIRE_SPARK_VY_MIN_Q8 + 1))
        );

        s->vx_q8 = (int16_t)((int)(rnd_u8() % 101) - 50);

        #if FIRE_DEBUG_COLOR_SPLIT
        s->r = FIRE_DEBUG_SPARK_R;
        s->g = FIRE_DEBUG_SPARK_G;
        s->b = FIRE_DEBUG_SPARK_B;
        #else
        /* choose color: blue/cyan/green */
        uint8_t sel = (uint8_t)(rnd_u8() % 3);
        if (sel == 0) { s->r = 10; s->g = 40;  s->b = 220; } // blue
        else if (sel == 1) { s->r = 10; s->g = 170; s->b = 200; } // cyan
        else { s->r = 10; s->g = 220; s->b = 50; } // green
        #endif


        s->life = (uint8_t)(FIRE_SPARK_LIFE_MIN + (rnd_u8() % (FIRE_SPARK_LIFE_MAX - FIRE_SPARK_LIFE_MIN + 1)));
        s->tail = (uint8_t)(rnd_u8() & 1 ? 0 : (uint8_t)(1 + (rnd_u8() & 1))); // point or tiny comet (1..2)
        return;
    }
#else
    (void)t_ms;
#endif
}

static void sparks_step_and_render(uint8_t bri, int16_t wind_q8)
{
#if FIRE_SPARKS_ENABLE
    for (int i = 0; i < FIRE_SPARKS_MAX; i++) {
        spark_t *s = &s_spk[i];
        if (!s->alive) continue;

        if (s->life == 0) { s->alive = false; continue; }
        s->life--;

        /* wind influence */
        s->vx_q8 = (int16_t)(s->vx_q8 + (wind_q8 / 60));

        s->x_q8 = (int16_t)(s->x_q8 + s->vx_q8);
        s->y_q8 = (int16_t)(s->y_q8 + s->vy_q8);

        int x = wrap_x((int)(s->x_q8 >> 8));
        int y = (int)(s->y_q8 >> 8);

        if (y < 0 || y >= FIRE_H) { s->alive = false; continue; }

        /* life fade: map remaining life -> 80..255 (never fully off mid-flight) */
        uint8_t life_k = (uint8_t)(
            80 + (uint16_t)s->life * 175u / (uint16_t)FIRE_SPARK_LIFE_MAX
        );

        /* render head + tail (additive) */
        for (int t = 0; t <= (int)s->tail; t++) {
            int yy = y - t;
            if (yy < 0) break;

            uint16_t cx, cy;
            map_to_canvas(x, yy, &cx, &cy);

            /* tail attenuation */
            uint8_t tail_k = 255;
            if (t == 1) tail_k = 140;
            else if (t >= 2) tail_k = 90;

            uint8_t k = scale_u8(life_k, tail_k);

            uint8_t r = scale_u8(s->r, k);
            uint8_t g = scale_u8(s->g, k);
            uint8_t b = scale_u8(s->b, k);

            /* apply global brightness (variant A uses bri passed in) */
            r = scale_u8(r, bri);
            g = scale_u8(g, bri);
            b = scale_u8(b, bri);

            uint8_t cr, cg, cb;
            (void)fx_canvas_get(cx, cy, &cr, &cg, &cb);
            cr = u8_clamp_i32((int)cr + r);
            cg = u8_clamp_i32((int)cg + g);
            cb = u8_clamp_i32((int)cb + b);
            fx_canvas_set(cx, cy, cr, cg, cb);
        }
    }
#else
    (void)bri; (void)wind_q8;
#endif
}


/* -------------------- Render field -------------------- */
static void fire_render_field(uint8_t bri)
{
    fx_canvas_clear(0, 0, 0);

    /* Render logical field to physical canvas with mapping */
    for (int ly = 0; ly < FIRE_H; ly++) {
        for (int lx = 0; lx < FIRE_W; lx++) {

            int ly_src = ly;

#if FIRE_TIP_PROFILE_ENABLE
            /* Apply tip-profile only in upper zone to increase ragged amplitude
             * without lifting the whole flame body.
             *
             * s_tip_delta_px[lx] > 0  => pull hotter samples from ниже (visual peak higher)
             * s_tip_delta_px[lx] < 0  => sample from выше (visual dip deeper)
             */
            if ((ly >= FIRE_TIP_APPLY_Y) && s_tip_init) {
                int sh = (int)s_tip_delta_px[lx];

                /* ramp: make "crown transition" thicker (blend-in over N pixels) */
                const int ramp_h = 10; /* <-- регулирует толщину переходной зоны */
                int w = ly - FIRE_TIP_APPLY_Y;    /* 0.. */
                if (w > ramp_h) w = ramp_h;

                /* effective shift = sh * w / ramp_h */
                int sh_eff = (sh * w) / ramp_h;

                ly_src = ly - sh_eff;

                if (ly_src < 0) ly_src = 0;
                if (ly_src >= FIRE_H) ly_src = FIRE_H - 1;
            }

#endif

            uint8_t h = s_heat[ly_src][lx];
            if (h == 0) continue;

            uint16_t cx, cy;
            map_to_canvas(lx, ly, &cx, &cy);

            uint8_t r, g, b;
            heat_to_rgb(h, bri, &r, &g, &b);

            /* Write (no additive here; additive reserved for petals/sparks overlays) */
            fx_canvas_set(cx, cy, r, g, b);
        }
    }
}


/* -------------------- Main effect -------------------- */
void fx_fire_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    if (!ctx) return;

    /* Detect entry/reset:
     * We assume engine resets ctx->frame on effect switch.
     * If not, this still resets on reboot (s_inited false).
     */
    if (!s_inited || ctx->frame == 0 || ctx->frame < s_last_seen_frame) {
        fire_reset(t_ms);
    }
    s_last_seen_frame = ctx->frame;

    /* paused => stop simulation, keep current frame */
    const bool paused = ctx->paused;

    /* brightness Variant A (+ floor for 0) */
    uint8_t bri = ctx->brightness;
    if (bri == 0) bri = FIRE_BRI_FLOOR0;

    /* speed_pct 10..300 -> step scale */
    uint16_t spd = ctx->speed_pct;
    if (spd < 10)  spd = 10;
    if (spd > 300) spd = 300;

    /* dt */
    uint32_t dt_ms = (t_ms >= s_last_ms) ? (t_ms - s_last_ms) : 0;
    s_last_ms = t_ms;
    if (dt_ms > FIRE_DT_CAP_MS) dt_ms = FIRE_DT_CAP_MS;

    if (!paused) {
        /* accumulate time and convert to simulation steps */
        s_accum_ms += dt_ms;

        /* ignition ramp 0..255 */
        if (s_ignite_ms < FIRE_IGNITE_MS) {
            s_ignite_ms += dt_ms;
            if (s_ignite_ms > FIRE_IGNITE_MS) s_ignite_ms = FIRE_IGNITE_MS;
        }
        uint8_t ignite_k = (uint8_t)((s_ignite_ms * 255u) / FIRE_IGNITE_MS);

        /* step size depends on speed_pct: smaller ms per step => more steps */
        uint32_t step_ms = (FIRE_BASE_STEP_MS * 100u) / (uint32_t)spd;
        if (step_ms < 8u) step_ms = 8u;

        while (s_accum_ms >= step_ms) {
            s_accum_ms -= step_ms;

            /* update controls */
            fire_wind_update(step_ms);
            fire_tongues_update(step_ms);
            fire_jets_update();

            /* inject fuel at base */
            fire_inject(ignite_k);

            /* hot islands */
            fire_islands();

            /* dynamics:
             * During jet we slightly boost upflow to push tongues higher (up to ~45),
             * while steady stays around 18..25 by cooling slope.
             */
            uint8_t up = FIRE_UPFLOW;
#if FIRE_JETS_ENABLE
            if (s_jet_life > 0) {
                int add = (FIRE_UPFLOW_JET_BOOST * (int)s_jet_life) / FIRE_JET_LIFE_STEPS;
                up = u8_clamp_i32((int)up + add);
            }
#endif
            fire_step_field(up, FIRE_DIFFUSE, FIRE_COOL_BASE, FIRE_COOL_Y_SLOPE, s_wind_q8);
#if FIRE_TIP_PROFILE_ENABLE
            int16_t tip_raw_q8[FIRE_W];
            for (int x = 0; x < FIRE_W; x++) {
                int tip = fire_tip_y_of_col(x);
                if (tip > FIRE_JET_TOP_Y) tip = FIRE_JET_TOP_Y;
                tip_raw_q8[x] = (int16_t)(tip << 8);
            }
            fire_tip_profile_update_q8(tip_raw_q8);
#endif

            /* spawn petals from ragged edge:
             * choose a few columns around current active flame top
             */
#if FIRE_PETALS_ENABLE
            for (int k = 0; k < 3; k++) {
                int x = (int)(rnd_u8() % FIRE_W);

                int tip  = fire_tip_y_of_col(x);
                int xL   = wrap_x(x - 1);
                int xR   = wrap_x(x + 1);
                int tipL = fire_tip_y_of_col(xL);
                int tipR = fire_tip_y_of_col(xR);

                if (tip  > FIRE_JET_TOP_Y) tip  = FIRE_JET_TOP_Y;
                if (tipL > FIRE_JET_TOP_Y) tipL = FIRE_JET_TOP_Y;
                if (tipR > FIRE_JET_TOP_Y) tipR = FIRE_JET_TOP_Y;

#if FIRE_TIP_PROFILE_ENABLE
                /* apply tip-profile delta so petals match the visually amplified ragged edge */
                tip  += (int)s_tip_delta_px[x];
                tipL += (int)s_tip_delta_px[xL];
                tipR += (int)s_tip_delta_px[xR];

                if (tip < 0) {
                tip = 0;
                } else if (tip > FIRE_JET_TOP_Y) {
                tip = FIRE_JET_TOP_Y;
                }

                if (tipL < 0) {
                tipL = 0;
                } else if (tipL > FIRE_JET_TOP_Y) {
                tipL = FIRE_JET_TOP_Y;
                }

                if (tipR < 0) {
                tipR = 0;
                } else if (tipR > FIRE_JET_TOP_Y) {
                tipR = FIRE_JET_TOP_Y;
                }

#endif

                int d = tip - (tipL + tipR) / 2;
                if (d < 0) d = -d;

                if (d >= 2 || (s_jet_life > 0)) {
                    petals_spawn_from_tip(x, tip, s_wind_q8);
                }
            }
#endif

        }

        /* sparks timing (ms based) */
        sparks_spawn(t_ms);
    }

    /* render base field */
    fire_render_field(bri);

    /* overlays (wind-coupled) */
    petals_step_and_render(bri, s_wind_q8);
    sparks_step_and_render(bri, s_wind_q8);

    /* push to WS2812 buffer */
    fx_canvas_present();
}
