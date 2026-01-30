// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Andrei Nechaev


// main/fx_effects_fire.c
#include "fx_engine.h"
#include "fx_canvas.h"

#include <stdint.h>
#include <stdbool.h>

#include "esp_random.h" // esp_random()

/* ============================================================
 * FIRE effect - USER TUNABLES (grouped, single-source-of-truth)
 *
 * Правило настройки:
 *  1) Цвет и вид (палитра, низ огня, islands white)
 *  2) Корона/кромка (TIP PROFILE)
 *  3) Тело/динамика (инжект, диффузия, подъём, ветер)
 *  4) События (islands/jets/petals/sparks)
 *
 * Важно:
 *  - Большинство параметров “в шагах” завязаны на FIRE_BASE_STEP_MS.
 *    Например life_steps * FIRE_BASE_STEP_MS ~= длительность в ms.
 * ============================================================ */


/* ==================== A) SERVICE (обычно НЕ трогать) ==================== */

/* --- A0) ORIENTATION / MAPPING ---
 * НЕ ТРОГАТЬ, если не менялась проводка/ориентация панелей.
 * Менять только если реально перевернул/поменял местами сегменты 16x16.
 */
#ifndef FIRE_STACK_REVERSE
#define FIRE_STACK_REVERSE          0   // 0=как есть; 1=сегменты по высоте перепутаны местами
#endif

#ifndef FIRE_ROW_INVERT
#define FIRE_ROW_INVERT             0   // 0=как есть; 1=инвертировать Y внутри каждого 16x16
#endif


/* --- A1) GEOMETRY / TIMING ---
 * Менять только если меняется геометрия/тайминг симуляции.
 */
#define FIRE_W                      16  // ширина поля, px. НЕ менять без пересчёта логики
#define FIRE_H                      48  // высота поля, px (3 панели 16x16 в стек)

#define FIRE_BASE_STEP_MS           40u // ↑ больше = медленнее сим, ↓ меньше = быстрее. Шаг: 5..10 ms. legasy (удалить?)
#ifndef FIRE_SIM_SPEED_PCT
#define FIRE_SIM_SPEED_PCT          100u  // 100=как сейчас. 120=быстрее, 80=медленнее (меняет step_ms).
#endif
#define FIRE_DT_CAP_MS              120u// cap провалов dt (лаг/пауза). Обычно не трогать


/* --- A2) SAFETY / BRIGHTNESS CLAMPS ---
 * Ограничители, чтобы эффект не "умирал" и не выходил за допустимые рамки.
 */
#define FIRE_BRI_FLOOR0             12  // если ctx->brightness==0: ↑ ярче в нуле, ↓ темнее. Шаг: 2..4
#define FIRE_BRI_MAX_CLAMP          255 // 255=без clamp. Обычно не трогать


/* ==================== B) DEBUG / VISUAL INSPECTION (сервис) ==================== */

/* --- B0) DEBUG MODE SWITCH ---
 * 0 = нормальная палитра (обычный режим)
 * 1 = цветовая сепарация элементов (debug-режим, симуляцию НЕ меняет)
 */
#define FIRE_DEBUG_COLOR_SPLIT      0   // ↑ поставить 1 чтобы видеть слои разными цветами

/* --- B1) DEBUG LAYER COLORS (RGB) ---
 * Только при FIRE_DEBUG_COLOR_SPLIT=1. Яркость будет масштабироваться под пиксель.
 */
#define FIRE_DEBUG_FIELD_R          255 // тело поля (FIELD)
#define FIRE_DEBUG_FIELD_G          60
#define FIRE_DEBUG_FIELD_B          0

#define FIRE_DEBUG_TONGUE_R         0   // языки у основания (TONGUES)
#define FIRE_DEBUG_TONGUE_G         255
#define FIRE_DEBUG_TONGUE_B         60

#define FIRE_DEBUG_JET_R            0   // jets / всплески (JETS)
#define FIRE_DEBUG_JET_G            140
#define FIRE_DEBUG_JET_B            255

#define FIRE_DEBUG_ISLAND_R         255 // hot islands (ISLANDS)
#define FIRE_DEBUG_ISLAND_G         0
#define FIRE_DEBUG_ISLAND_B         0

#define FIRE_DEBUG_TIP_R            180 // корона/кромка (TIP/CROWN)
#define FIRE_DEBUG_TIP_G            0
#define FIRE_DEBUG_TIP_B            255

#define FIRE_DEBUG_PETAL_R          255 // лепестки (PETALS)
#define FIRE_DEBUG_PETAL_G          110
#define FIRE_DEBUG_PETAL_B          0

#define FIRE_DEBUG_SPARK_R          80  // искры (SPARKS)
#define FIRE_DEBUG_SPARK_G          0
#define FIRE_DEBUG_SPARK_B          255


/* ==================== C) COLORS / LOOK (обычный режим) ==================== */

/* --- C0) WOOD FLAME TINT ( настраиваемая палитра тела, костыль (рабочий), так как изменять базовый heat цвет - не способен) ---
 * Рекомендация:
 *  - Сначала выставь MIX_Q8, потом подгони RGB цели.*/

 /* 1) Быстрый "анти-зеленца" скейл (legacy, как тело лампы использует базовой палитры - зеленоватый) */

#define FIRE_WOOD_G_SCALE_Q8        180 // ↓ меньше = теплее/желтее, ↑ больше = ближе к исходному(256). Шаг: 10..20

/* 2) Художественный tint-mix (как у лепестков): смешиваем базовую палитру с целевым "wood flame" тоном */
#define FIRE_BODY_TINT_MIX_Q8       140 // ↑ больше = ближе к "wood flame" RGB, ↓ меньше = ближе к базовой палитре

/* Целевой тон оранжевого к которому придем если FIRE_PETAL_ORANGE_MIX_Q8 = 255*/
#define FIRE_BODY_TINT_R            255 // обычно 255
#define FIRE_BODY_TINT_G            210 // ↓ меньше = краснее/янтарнее, ↑ больше = желтее
#define FIRE_BODY_TINT_B            20  // ↑ больше = теплее-белее (но аккуратно, чтобы не "молоко")

/* Физика процессов, не графика. */
#define FIRE_VIS_COOL_ROWS          4   // ↑ больше = выше зона охлаждения, ↓ меньше = тоньше. Шаг: 1..2
#define FIRE_VIS_COOL_MIN_Q8        20  // ↓ меньше = сильнее затемнение низа. Шаг: 10..30 (0..256)


/* ==================== D) STARTUP / IGNITION (разгорание) ==================== */

/* Влияет только на стартовую вспышку/переход в steady */
#define FIRE_IGNITE_MS              650u// ↑ дольше разгорается, ↓ быстрее. Шаг: 100..200 ms
#define FIRE_IGNITE_BOOST           80  // ↑ сильнее стартовый жар, ↓ мягче. Шаг: 10..20


/* ==================== E) GLOBAL LIMITS / HEIGHT ZONES ==================== */

/* Ограничители высот/зон. Менять аккуратно, легко "сломать композицию". */
#define FIRE_TIP_STEADY_MIN         10  // ↑ выше базовая кромка, ↓ ниже. Шаг: 1..2 px
#define FIRE_TIP_STEADY_MAX         42  // ↑ выше "потолок" кромки, ↓ ниже. Шаг: 1..2 px
#define FIRE_JET_TOP_Y              50  // ↑ jets могут целиться выше (логика клипает). Обычно не трогать
#define FIRE_PETAL_ABOVE_Y          45  // ↑ лепестки могут улетать выше, ↓ ниже. Шаг: 1..2 px


/* ==================== F) FIELD DYNAMICS (тело пламени) ==================== */

/* База: “топливо” снизу + шум по колонкам */
#define FIRE_BASE_INJECT            140 // ↑ сильнее горит снизу, ↓ слабее. Шаг: 10..20
#define FIRE_INJECT_NOISE           250 // ↑ больше разброс/живость, ↓ ровнее. Шаг: 10..30

/* Охлаждение (затухание) */
#define FIRE_COOL_BASE              6   // ↑ быстрее тухнет, ↓ жарче/дольше живёт. Шаг: 1..2
#define FIRE_COOL_Y_SLOPE           3   // ↑ верх гаснет сильнее, ↓ верх живее. Шаг: 1

/* Размытие/подъём */
#define FIRE_DIFFUSE                2   // ↑ гладче/смазаннее, ↓ резче структуры. Шаг: 1
#define FIRE_UPFLOW                 205 // ↑ сильнее тянет вверх (живее), ↓ тяжелее. Шаг: 5..15
#define FIRE_UPFLOW_JET_BOOST       150 // ↑ jets сильнее пробивают вверх, ↓ мягче. Шаг: 10..30


/* ==================== G) WIND (качание по X) ==================== */

/* Ветер влияет и на тело, и на перенос частиц/лепестков. */
#define FIRE_WIND_JITTER_Q8         120 // ↑ больше мелкой дрожи, ↓ спокойнее. Шаг: 20..80
#define FIRE_WIND_GUST_ENABLE       1   // 1=порывы вкл, 0=только базовый ветер
#define FIRE_WIND_GUST_Q8           250 // ↑ сильнее порывы, ↓ слабее. Шаг: 20..80
#define FIRE_WIND_RESP              28  // ↑ быстрее меняется ветер, ↓ более вязко. Шаг: 2..6


/* ==================== H) TONGUES (языки у основания) ==================== */

/* Языки формируют “скелет” пламени (ширина/мощность/дрейф). */
#define FIRE_TONGUES                4   // ↑ больше языков (может стать шумно), ↓ меньше. Шаг: 1
#define FIRE_TONGUE_W_MIN           3   // ↑ шире минимально, ↓ уже. Шаг: 1
#define FIRE_TONGUE_W_MAX           6   // ↑ шире максимально, ↓ уже. Шаг: 1
#define FIRE_TONGUE_PWR_MIN         80 // ↑ горячее минимально, ↓ мягче. Шаг: 10..20
#define FIRE_TONGUE_PWR_MAX         190 // ↑ горячее максимально, ↓ мягче. Шаг: 10..20
#define FIRE_TONGUE_DRIFT_Q8        22  // ↑ быстрее гуляют по X, ↓ стабильнее. Шаг: 5..15


/* ==================== I) HOT ISLANDS (lava-lamp blobs внутри тела) ==================== */

/* Внутренние “горячие кляксы” (сполохи), живут секундами, мигрируют по X/Y. */
#define FIRE_ISLANDS_ENABLE         1   // 1=вкл, 0=выкл
#define FIRE_ISLANDS_RATE           24  // ↓ меньше = чаще спавн, ↑ больше = реже. Шаг: 2..6
#define FIRE_ISLANDS_Y_MIN          6   // ↓ меньше = ближе к низу, ↑ больше = выше. Шаг: 1
#define FIRE_ISLANDS_Y_MAX          15  // ↑ больше = выше зона islands, ↓ меньше = ниже. Шаг: 1
#define FIRE_ISLANDS_PWR            100 // ↑ ярче/горячее blobs, ↓ мягче. Шаг: 10..30

/* Сколько blobs одновременно (по твоему требованию держим <=5, но настраиваемо) */
#define FIRE_ISLANDS_MAX            3   // ↑ больше blobs, ↓ меньше. Шаг: 1 (рекоменд. 2..5)

/* Длительность жизни (в шагах). Время ~= steps * FIRE_BASE_STEP_MS */
#define FIRE_ISLANDS_LIFE_MIN_STEPS 100 // ↑ дольше живут (секунды), ↓ короче. Шаг: 10..20
#define FIRE_ISLANDS_LIFE_MAX_STEPS 180 // ↑ больше разброс/дольше макс, ↓ короче. Шаг: 10..30

/* Размер blob (радиус в пикселях) */
#define FIRE_ISLANDS_R_MIN_PX       1   // ↑ крупнее старт, ↓ мельче. Шаг: 1
#define FIRE_ISLANDS_R_MAX_PX       3   // ↑ крупнее пик, ↓ мельче. Шаг: 1

/* Скорость миграции (Q8 px/step). Больше = быстрее плывут */
#define FIRE_ISLANDS_VX_Q8          45  // ↑ быстрее по X, ↓ медленнее. Шаг: 5..15
#define FIRE_ISLANDS_VY_Q8          25  // ↑ быстрее по Y, ↓ медленнее. Шаг: 3..10

/* “Кривизна” траектории: насколько виляют и как быстро меняют направление */
#define FIRE_ISLANDS_CURVE_ACC_Q8   10  // ↑ сильнее виляют, ↓ прямее. Шаг: 2..5
#define FIRE_ISLANDS_CURVE_RESP_Q8  18  // ↑ резче меняют кривизну, ↓ плавнее. Шаг: 2..6

/* Неровность контура blob (0..255). Больше = более рваный край */
#define FIRE_ISLANDS_SHAPE_NOISE_Q8 80  // ↑ рванее, ↓ ровнее. Шаг: 10..30

/* Визуал: сделать islands белыми в обычном режиме (НЕ debug) */
#define FIRE_ISLANDS_WHITE_ENABLE   1   // 1=белеют, 0=как палитра тела
#define FIRE_ISLANDS_WHITE_MIX_Q8   80 // ↑ ближе к белому, ↓ ближе к телу. Шаг: 20..40
#define FIRE_ISLANDS_WHITE_MODE     2 // 1 = legacy (к 255), 2 = preserve luma (к текущей яркости пикселя),* 3 = clamp to global bri (к bri)



/* ==================== J) JETS / FLARES (редкие сильные всплески) ==================== */

/* Jets должны оставаться “в своём месте”, они отдельные от islands. */
#define FIRE_JETS_ENABLE            1   // 1=вкл, 0=выкл
#define FIRE_JET_RATE               22  // ↓ меньше = чаще jets, ↑ больше = реже. Шаг: 2..6
#define FIRE_JET_LIFE_STEPS         25  // ↑ дольше живут, ↓ короче. Шаг: 3..6
#define FIRE_JET_PWR                255 // сила jets (обычно 255). ↓ меньше = мягче. Шаг: 10..30
#define FIRE_JET_WIDTH_MIN          2   // ↑ шире минимум, ↓ уже. Шаг: 1
#define FIRE_JET_WIDTH_MAX          4   // ↑ шире максимум, ↓ уже. Шаг: 1


/* ==================== K) PETALS (оторванные фрагменты кромки) ==================== */

/* Лепестки - “красота” над короной, подхватываются ветром. */
#define FIRE_PETALS_ENABLE          1   // 1=вкл, 0=выкл
#define FIRE_PETALS_MAX             5   // ↑ больше одновременно, ↓ меньше. Шаг: 1..2
#define FIRE_PETAL_RATE             15  // ↓ меньше = чаще спавн, ↑ больше = реже. Шаг: 2..5
#define FIRE_PETAL_AREA_MIN         3   // ↑ крупнее минимум, ↓ мельче. Шаг: 1
#define FIRE_PETAL_AREA_MAX         9   // ↑ крупнее максимум, ↓ мельче. Шаг: 1
#define FIRE_PETAL_LIFE_MIN         140 // ↑ дольше живут, ↓ короче. Шаг: 20..40
#define FIRE_PETAL_LIFE_MAX         260 // ↑ дольше макс, ↓ короче. Шаг: 20..40
#define FIRE_PETAL_VY_Q8            210 // ↑ быстрее вверх, ↓ медленнее. Шаг: 20..60
#define FIRE_PETAL_VX_Q8            120 // ↑ сильнее ветром, ↓ прямее вверх. Шаг: 20..60


#define FIRE_PETAL_ORANGE_MIX_Q8   230   // ↓ к 0 = белее, ↑ к 255 = оранжевее. По сути ползунок между белым и оранжевым. Шаг 20.

/* Целевой тон оранжевого к которому придем если FIRE_PETAL_ORANGE_MIX_Q8 = 255*/
#define FIRE_PETAL_ORANGE_R        255   // обычно 255 (красный канал)
#define FIRE_PETAL_ORANGE_G        120   // ↓ меньше = более красный, ↑ больше = желтее
#define FIRE_PETAL_ORANGE_B        0     // ↑ больше = уходит в “теплый белый”




/* ==================== L) SPARKS (искры у основания) ==================== */

/* Искры - отдельные частицы у низа (точки/микро-кометы). */
#define FIRE_SPARKS_ENABLE          1   // 1=вкл, 0=выкл
#define FIRE_SPARKS_MAX             12  // ↑ больше одновременно, ↓ меньше. Шаг: 2..4

#define FIRE_SPARK_MIN_MS           2500u // ↑ реже, ↓ чаще. Шаг: 500..1000 ms
#define FIRE_SPARK_MAX_MS           8000u // ↑ реже (длиннее пауза), ↓ чаще. Шаг: 500..1500 ms

#define FIRE_SPARK_LIFE_MIN         30  // ↑ дольше тянутся, ↓ короче. Шаг: 5..10
#define FIRE_SPARK_LIFE_MAX         75  // ↑ дольше макс, ↓ короче. Шаг: 5..15

#define FIRE_SPARK_VY_MIN_Q8        140 // ↑ быстрее вверх, ↓ медленнее. Шаг: 20..40
#define FIRE_SPARK_VY_MAX_Q8        260 // ↑ быстрее макс, ↓ медленнее. Шаг: 20..60


/* ==================== M) TIP PROFILE (кромка + корона) ==================== */

/* Главный блок кромки. Если “ломает тело” - поднимай APPLY_Y или снижай ramp_gain. */
#define FIRE_TIP_PROFILE_ENABLE     1   // 1=вкл, 0=выкл (не рекомендуется выключать)

#define FIRE_TIP_APPLY_Y            13  // ↑ выше корона, ↓ глубже в тело. Шаг: 1..2 px
#define FIRE_TIP_RAMP_H             14  // ↑ толще/мягче переход, ↓ тоньше/резче. Шаг: 2..4 px

#define FIRE_TIP_RAMP_GAIN_Q8       250 // ↑ сильнее движение внизу короны, ↓ более “верхнее”. Шаг: 10..30
#define FIRE_TIP_MAX_SHIFT_PX       25  // ↑ допускает сильнее сдвиг, ↓ аккуратнее. Шаг: 2..4 px

#define FIRE_TIP_AMP_Q8             1100// ↑ рванее пики, ↓ спокойнее. Шаг: 80..160

#define FIRE_TIP_RISE_Q8            80  // ↑ быстрее поднимается, ↓ вязче. Шаг: 5..15
#define FIRE_TIP_FALL_Q8            25  // ↑ быстрее падает, ↓ дольше держит. Шаг: 5..15

#define FIRE_TIP_DRIVE_ENABLE       1   // 1=вкл активность, 0=выкл (кромка спокойнее)

#define FIRE_TIP_DRIVE_AMP_Q8       1000// ↑ сильнее/чаще заметно, ↓ мягче. Шаг: 80..160 (Q8 px)
#define FIRE_TIP_DRIVE_RESP_Q8      55  // ↑ резче реагирует, ↓ плавнее. Шаг: 5..15

#define FIRE_TIP_DRIVE_MIN_MS       60  // ↓ меньше = чаще новые цели, ↑ больше = спокойнее. Шаг: 20..60 ms
#define FIRE_TIP_DRIVE_MAX_MS       180 // ↑ больше = реже смена целей, ↓ меньше = чаще. Шаг: 20..80 ms

/* --- TIP COLOR GRADIENT (visual only, normal mode) ---
 * Делает корону/кромку отличимой по цвету от тела пламени.
 * Применяется ТОЛЬКО в зоне ramp: [FIRE_TIP_APPLY_Y .. FIRE_TIP_APPLY_Y+FIRE_TIP_RAMP_H]
 * 0 = выключено, 255 = полный переход к целевому TIP RGB на верхней части ramp.
 */
#define FIRE_TIP_COLOR_ENABLE        1   // 1=вкл, 0=выкл
#define FIRE_TIP_COLOR_MIX_MAX_Q8    220 // ↑ больше = сильнее “фиолетовая корона”, ↓ меньше = мягче. Шаг: 20..40

/* TIP gradient target (normal mode): from brighter/orange at bottom -> dark red at top */
#define FIRE_TIP_COLOR_LOW_R        255  // низ кромки: яркий красно-оранжевый
#define FIRE_TIP_COLOR_LOW_G        60
#define FIRE_TIP_COLOR_LOW_B        0

#define FIRE_TIP_COLOR_HIGH_R       220  // верх кромки: тёмно-красный
#define FIRE_TIP_COLOR_HIGH_G       0
#define FIRE_TIP_COLOR_HIGH_B       0








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
#if ((FIRE_W & (FIRE_W - 1)) == 0)
    // FIRE_W is power of two (16). Fast wrap via bitmask.
    return (x & (FIRE_W - 1));
#else
    while (x < 0) x += FIRE_W;
    while (x >= FIRE_W) x -= FIRE_W;
    return x;
#endif
}



static inline uint8_t heat_get(const uint8_t h[FIRE_H][FIRE_W], int x, int y)
{
    if (y < 0) y = 0;
    if (y >= FIRE_H) y = FIRE_H - 1;
    x = wrap_x(x);
    return h[y][x];
}

/* Map logical (lx:0..15, ly:0..47, ly=0 bottom) -> physical canvas (x:0..15, y:0..47, y=0 top) */
static inline void map_to_canvas(int lx, int ly, uint16_t *cx, uint16_t *cy)
{
    int seg = ly / 16;   // 0 bottom, 1 mid, 2 top
    int row = ly % 16;   // 0..15 внутри сегмента (0 = bottom в logical)

#if FIRE_STACK_REVERSE
    seg = 2 - seg;
#endif

#if FIRE_ROW_INVERT
    row = 15 - row;
#endif

    /* ly2 всё ещё “снизу вверх” (bottom-origin) */
    int ly2 = seg * 16 + row;
    int y   = ly2;


    *cx = (uint16_t)lx;
    *cy = (uint16_t)y;
}

/* -------------------- Render fast-path tables -------------------- */
static uint16_t s_map_cx[FIRE_H][FIRE_W];
static uint16_t s_map_cy[FIRE_H][FIRE_W];

#if FIRE_TIP_PROFILE_ENABLE
static uint8_t s_tip_ramp_q8_by_ly[FIRE_H];
#endif

static bool s_tables_inited = false;

static void fire_build_tables_once(void)
{
    if (s_tables_inited) return;

    /* 1) logical->canvas mapping table */
    for (int ly = 0; ly < FIRE_H; ly++) {
        for (int lx = 0; lx < FIRE_W; lx++) {
            uint16_t cx, cy;
            map_to_canvas(lx, ly, &cx, &cy);
            s_map_cx[ly][lx] = cx;
            s_map_cy[ly][lx] = cy;
        }
    }

#if FIRE_TIP_PROFILE_ENABLE
    /* 2) tip ramp by Y (only depends on ly) */
    for (int ly = 0; ly < FIRE_H; ly++) {
        uint8_t ramp_q8 = 0;

        if (ly >= FIRE_TIP_APPLY_Y) {
            const int ramp_h = FIRE_TIP_RAMP_H;
            int w = ly - FIRE_TIP_APPLY_Y;      /* 0.. */
            if (w > ramp_h) w = ramp_h;

            /* ramp_q8 = w*255/ramp_h */
            ramp_q8 = (uint8_t)((w * 255) / ramp_h);

            /* усилить нижнюю часть короны */
            ramp_q8 = (uint8_t)u8_clamp_i32(((int)ramp_q8 * FIRE_TIP_RAMP_GAIN_Q8) >> 8);
        }

        s_tip_ramp_q8_by_ly[ly] = ramp_q8;
    }
#endif

    s_tables_inited = true;
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

    /* Wood tint: pull green down a bit to avoid “greenish” body (visual only) */
    G = (uint8_t)(((uint32_t)G * (uint32_t)FIRE_WOOD_G_SCALE_Q8) >> 8);
        /* Optional body tint-mix (like petals): mix base palette -> target RGB, preserving brightness */
    if (FIRE_BODY_TINT_MIX_Q8) {
        /* keep current brightness (already palette-space, before bri scaling) */
        uint8_t tr = FIRE_BODY_TINT_R;
        uint8_t tg = FIRE_BODY_TINT_G;
        uint8_t tb = FIRE_BODY_TINT_B;

        R = u8_lerp(R, tr, (uint8_t)FIRE_BODY_TINT_MIX_Q8);
        G = u8_lerp(G, tg, (uint8_t)FIRE_BODY_TINT_MIX_Q8);
        B = u8_lerp(B, tb, (uint8_t)FIRE_BODY_TINT_MIX_Q8);
    }



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

#if FIRE_ISLANDS_ENABLE && FIRE_ISLANDS_WHITE_ENABLE
/* Маска для визуального выделения islands в обычном режиме (0..255). */
static uint8_t s_island_mark[FIRE_H][FIRE_W];
#endif


#if FIRE_DEBUG_COLOR_SPLIT
/* Пиксельная метка: “в этом кадре здесь был island”.
 * Только для визуальной сепарации, на симуляцию не влияет.
 */
static uint8_t s_dbg_island[FIRE_H][FIRE_W];

enum {
    DBG_L_FIELD  = 0,
    DBG_L_TONGUE = 1,
    DBG_L_JET    = 2,
    DBG_L_ISLAND = 3,
    DBG_L_TIP    = 4,
};

static inline uint8_t u8_max3(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m = (a > b) ? a : b;
    return (m > c) ? m : c;
}

static inline void dbg_apply_layer(uint8_t layer, uint8_t k, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t lr = FIRE_DEBUG_FIELD_R, lg = FIRE_DEBUG_FIELD_G, lb = FIRE_DEBUG_FIELD_B;

    switch (layer) {
        case DBG_L_TONGUE: lr = FIRE_DEBUG_TONGUE_R; lg = FIRE_DEBUG_TONGUE_G; lb = FIRE_DEBUG_TONGUE_B; break;
        case DBG_L_JET:    lr = FIRE_DEBUG_JET_R;    lg = FIRE_DEBUG_JET_G;    lb = FIRE_DEBUG_JET_B;    break;
        case DBG_L_ISLAND: lr = FIRE_DEBUG_ISLAND_R; lg = FIRE_DEBUG_ISLAND_G; lb = FIRE_DEBUG_ISLAND_B; break;
        case DBG_L_TIP:    lr = FIRE_DEBUG_TIP_R;    lg = FIRE_DEBUG_TIP_G;    lb = FIRE_DEBUG_TIP_B;    break;
        default: break;
    }

    /* сохраняем “яркость” k, меняем только цвет */
    *r = scale_u8(lr, k);
    *g = scale_u8(lg, k);
    *b = scale_u8(lb, k);
}
#endif /* FIRE_DEBUG_COLOR_SPLIT */


#if FIRE_TIP_PROFILE_ENABLE
static bool    s_tip_init = false;
static int16_t s_tip_filt_q8[FIRE_W];     /* filtered tip (Q8) */
static int16_t s_tip_delta_q8[FIRE_W];    /* amplified delta vs mean (Q8, signed) */
static int8_t  s_tip_delta_px[FIRE_W];    /* delta in pixels (signed), clamped */

#if FIRE_TIP_DRIVE_ENABLE
static int16_t  s_tip_drive_q8[FIRE_W];       /* current drive (Q8 px, signed) */
static int16_t  s_tip_drive_tgt_q8[FIRE_W];   /* target drive (Q8 px, signed) */
static uint16_t s_tip_drive_timer_ms[FIRE_W]; /* retarget timers */
#endif
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
    fire_build_tables_once();
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

typedef struct {
    bool     active;
    uint16_t age_steps;
    uint16_t life_steps;

    int32_t  x_q8;
    int32_t  y_q8;

    int16_t  vx_q8;
    int16_t  vy_q8;

    int16_t  ax_q8;     /* “кривизна” как плавное ускорение */
    int16_t  ay_q8;

    uint32_t rng;       /* локальный RNG островка */
} fire_island_t;

static fire_island_t s_islands[FIRE_ISLANDS_MAX];

/* Отдельный RNG для islands, чтобы не сдвигать rnd_* для остальных подсистем */
static uint32_t s_islands_rng = 0xC001D00Du;

static inline uint32_t islands_rng_next(uint32_t *s)
{
    /* xorshift32 */
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline uint8_t islands_rand_u8(uint32_t *s)
{
    return (uint8_t)(islands_rng_next(s) & 0xFF);
}

/* 0..255 огибающая: 0->255->0 по жизни (без float) */
static inline uint8_t islands_env_u8(uint16_t age, uint16_t life)
{
    if (life == 0) return 0;
    uint16_t half = (uint16_t)(life >> 1);
    uint16_t a = (age <= half) ? age : (uint16_t)(life - age);
    /* нормируем a/half в 0..255 */
    if (half == 0) return 0;
    uint32_t v = ((uint32_t)a * 255u) / (uint32_t)half;
    if (v > 255u) v = 255u;
    /* чуть сгладим (квадрат): v = (v*v)/255 */
    v = (v * v) / 255u;
    return (uint8_t)v;
}


static void fire_islands(void)
{
#if FIRE_ISLANDS_ENABLE
    /* 1) Спавн: сохраняем прежнюю схему rnd_u8(), чтобы не менять RNG-поток проекта.
     * Было: 1 вызов на проверку + 3 вызова на x/y/rad. Оставляем те же 4 вызова.
     */
    uint8_t r0 = rnd_u8();
    if ((r0 % FIRE_ISLANDS_RATE) == 0) {

        uint8_t rx = rnd_u8();
        uint8_t ry = rnd_u8();
        uint8_t rr = rnd_u8(); /* раньше был rad, теперь используем как часть seed/вариаций */

        /* найти слот */
        int slot = -1;
        for (int i = 0; i < FIRE_ISLANDS_MAX; i++) {
            if (!s_islands[i].active) { slot = i; break; }
        }

        if (slot >= 0) {
            fire_island_t *p = &s_islands[slot];
            p->active = true;
            p->age_steps = 0;

            /* жизнь в шагах (секунды) */
            uint16_t life_span = (uint16_t)(FIRE_ISLANDS_LIFE_MAX_STEPS - FIRE_ISLANDS_LIFE_MIN_STEPS + 1);
            p->life_steps = (uint16_t)(FIRE_ISLANDS_LIFE_MIN_STEPS + (uint16_t)(rr % life_span));

            int x0 = (int)(rx % FIRE_W);
            int y0 = FIRE_ISLANDS_Y_MIN + (int)(ry % (FIRE_ISLANDS_Y_MAX - FIRE_ISLANDS_Y_MIN + 1));

            p->x_q8 = (int32_t)x0 << 8;
            p->y_q8 = (int32_t)y0 << 8;

            /* локальный seed */
            p->rng = (uint32_t)0x9E3779B9u ^ ((uint32_t)rx << 16) ^ ((uint32_t)ry << 8) ^ (uint32_t)rr;
            p->rng ^= islands_rng_next(&s_islands_rng);

            /* базовые скорости */
            int16_t vx = (int16_t)FIRE_ISLANDS_VX_Q8;
            int16_t vy = (int16_t)FIRE_ISLANDS_VY_Q8;

            /* рандом направления */
            if (rx & 1) vx = (int16_t)-vx;
            if (ry & 1) vy = (int16_t)-vy;

            p->vx_q8 = vx;
            p->vy_q8 = vy;

            p->ax_q8 = 0;
            p->ay_q8 = 0;
        }
    }

    /* 2) Step + Render: живущие blobs мигрируют и “дышат” по огибающей, добавляя тепло в s_heat */
    for (int i = 0; i < FIRE_ISLANDS_MAX; i++) {
        fire_island_t *p = &s_islands[i];
        if (!p->active) continue;

        if (p->age_steps >= p->life_steps) {
            p->active = false;
            continue;
        }

        uint8_t env = islands_env_u8(p->age_steps, p->life_steps); /* 0..255 */

        /* радиус по огибающей: r = rmin + (rmax-rmin)*env/255 */
        int rmin = FIRE_ISLANDS_R_MIN_PX;
        int rmax = FIRE_ISLANDS_R_MAX_PX;
        int r = rmin + (int)(((int32_t)(rmax - rmin) * env + 127) / 255);
        if (r < 1) r = 1;

        /* “кривизна”: плавно меняем ускорение, затем скорость */
        {
            uint8_t n = islands_rand_u8(&p->rng); /* локальный RNG */
            int16_t ax_tgt = (int16_t)((((int)(n & 0x0F) - 8) * FIRE_ISLANDS_CURVE_ACC_Q8)); /* примерно -80..+80 Q8 */
            int16_t ay_tgt = (int16_t)((((int)((n >> 4) & 0x0F) - 8) * FIRE_ISLANDS_CURVE_ACC_Q8));

            p->ax_q8 = (int16_t)(p->ax_q8 + (int16_t)(((int32_t)FIRE_ISLANDS_CURVE_RESP_Q8 * (ax_tgt - p->ax_q8)) >> 8));
            p->ay_q8 = (int16_t)(p->ay_q8 + (int16_t)(((int32_t)FIRE_ISLANDS_CURVE_RESP_Q8 * (ay_tgt - p->ay_q8)) >> 8));

            p->vx_q8 = (int16_t)(p->vx_q8 + (p->ax_q8 >> 4)); /* маленький вклад */
            p->vy_q8 = (int16_t)(p->vy_q8 + (p->ay_q8 >> 4));
        }

        /* перемещение */
        p->x_q8 += p->vx_q8;
        p->y_q8 += p->vy_q8;

        int x0 = wrap_x((int)(p->x_q8 >> 8));
        int y0 = (int)(p->y_q8 >> 8);

        /* отражение по Y в пределах тела */
        if (y0 < FIRE_ISLANDS_Y_MIN) {
            y0 = FIRE_ISLANDS_Y_MIN;
            p->y_q8 = (int32_t)y0 << 8;
            p->vy_q8 = (int16_t)-p->vy_q8;
        } else if (y0 > FIRE_ISLANDS_Y_MAX) {
            y0 = FIRE_ISLANDS_Y_MAX;
            p->y_q8 = (int32_t)y0 << 8;
            p->vy_q8 = (int16_t)-p->vy_q8;
        }

        /* мощность: базовая * env */
        int pwr = (int)(((int32_t)FIRE_ISLANDS_PWR * env + 127) / 255);
        if (pwr <= 0) { p->age_steps++; continue; }

        /* рендер blob в heat: мягкая “клякса” с неровным краем */
        for (int dy = -r; dy <= r; dy++) {
            int y = y0 + dy;
            if (y < 0 || y >= FIRE_H) continue;

            for (int dx = -r; dx <= r; dx++) {
                int x = wrap_x(x0 + dx);

                int d2 = dx*dx + dy*dy;
                int r2 = r*r;

                /* базовый профиль: чем ближе к центру, тем больше add */
                if (d2 > r2) continue;

                /* неровный край: сдвигаем “порог” через хеш (локальный RNG) */
                uint32_t h = (uint32_t)(x * 131u + y * 313u) ^ p->rng;
                uint8_t n = (uint8_t)((h ^ (h >> 8)) & 0xFF);
                int edge = (int)(((int32_t)(n - 128) * FIRE_ISLANDS_SHAPE_NOISE_Q8) >> 8); /* ~[-noise..+noise] */
                int r2_eff = r2 + edge;
                if (r2_eff < 1) r2_eff = 1;
                if (d2 > r2_eff) continue;

                int add = pwr - (d2 * (pwr / (r2 + 1) + 1)); /* плавное падение */
                if (add <= 0) continue;

                s_heat[y][x] = u8_clamp_i32((int)s_heat[y][x] + add);
#if FIRE_ISLANDS_WHITE_ENABLE
                /* mark strength ~ env (0..255), keep max */
                if (env > s_island_mark[y][x]) s_island_mark[y][x] = env;
#endif

#if FIRE_DEBUG_COLOR_SPLIT
                s_dbg_island[y][x] = 1;
#endif
            }
        }

        p->age_steps++;
    }
#endif /* FIRE_ISLANDS_ENABLE */
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

            /* diffusion (neighbor average) - inline fast path */
            const int xm1 = wrap_x(x - 1);
            const int xp1 = wrap_x(x + 1);

            const uint8_t n0 = s_heat[y][x];
            const uint8_t n1 = s_heat[y][xm1];
            const uint8_t n2 = s_heat[y][xp1];
            const uint8_t n3 = s_heat[y - 1][x];
            const uint8_t n4 = (y + 1 < FIRE_H) ? s_heat[y + 1][x] : s_heat[FIRE_H - 1][x];

            const uint8_t avg = (uint8_t)((uint16_t)(n0 + n1 + n2 + n3 + n4) / 5u);


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
static void fire_tip_profile_update_q8(int16_t tip_raw_q8[FIRE_W], uint32_t dt_ms)
{
#if FIRE_TIP_DRIVE_ENABLE
    /* drive: per-column target noise with smoothing (adds "activity") */
    for (int x = 0; x < FIRE_W; x++) {
        if (s_tip_drive_timer_ms[x] <= dt_ms) {
            /* retarget */
            uint16_t span = (uint16_t)(FIRE_TIP_DRIVE_MAX_MS - FIRE_TIP_DRIVE_MIN_MS + 1);
            s_tip_drive_timer_ms[x] = (uint16_t)(FIRE_TIP_DRIVE_MIN_MS + (rnd_u32() % span));

            int r = (int)(rnd_u8()) - 128; /* -128..127 */
            s_tip_drive_tgt_q8[x] = (int16_t)(((int32_t)r * (int32_t)FIRE_TIP_DRIVE_AMP_Q8) / 128);
        } else {
            s_tip_drive_timer_ms[x] = (uint16_t)(s_tip_drive_timer_ms[x] - dt_ms);
        }

        int16_t cur = s_tip_drive_q8[x];
        int16_t tgt = s_tip_drive_tgt_q8[x];
        int16_t d   = (int16_t)(tgt - cur);

        cur = (int16_t)(cur + (int16_t)(((int32_t)FIRE_TIP_DRIVE_RESP_Q8 * (int32_t)d) >> 8));
        s_tip_drive_q8[x] = cur;

        /* apply to raw tip (clamp) */
        int32_t tr = (int32_t)tip_raw_q8[x] + (int32_t)cur;
        int32_t lo = 0;
        int32_t hi = ((int32_t)FIRE_JET_TOP_Y << 8);
        if (tr < lo) tr = lo;
        if (tr > hi) tr = hi;
        tip_raw_q8[x] = (int16_t)tr;
    }
#else
    (void)dt_ms;
#endif

    if (!s_tip_init) {
        for (int x = 0; x < FIRE_W; x++) {
            s_tip_filt_q8[x]  = tip_raw_q8[x];
            s_tip_delta_q8[x] = 0;
            s_tip_delta_px[x] = 0;
        }
#if FIRE_TIP_DRIVE_ENABLE
        for (int x = 0; x < FIRE_W; x++) {
            s_tip_drive_q8[x] = 0;
            s_tip_drive_tgt_q8[x] = 0;
            s_tip_drive_timer_ms[x] = 0;
        }
#endif
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
        int16_t rel = (int16_t)(s_tip_filt_q8[x] - mean); /* Q8 signed */
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
    /* Normal mode petals: tint from white -> orange (does not depend on body palette) */
    uint8_t ph = p->heat;

    /* brightness base from petal heat */
    uint8_t k  = scale_u8(ph, bri);     /* 0..255 */

    /* white (w) and orange (o) at same brightness k */
    uint8_t wr = k, wg = k, wb = k;
    uint8_t or = scale_u8(FIRE_PETAL_ORANGE_R, k);
    uint8_t og = scale_u8(FIRE_PETAL_ORANGE_G, k);
    uint8_t ob = scale_u8(FIRE_PETAL_ORANGE_B, k);

    /* mix control: 0=white, 255=orange */
    r = u8_lerp(wr, or, (uint8_t)FIRE_PETAL_ORANGE_MIX_Q8);
    g = u8_lerp(wg, og, (uint8_t)FIRE_PETAL_ORANGE_MIX_Q8);
    b = u8_lerp(wb, ob, (uint8_t)FIRE_PETAL_ORANGE_MIX_Q8);
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
            uint8_t tip_ramp_q8 = 0; /* 0..255: насколько мы внутри зоны короны (visual weight) */

    #if FIRE_TIP_PROFILE_ENABLE
            if ((ly >= FIRE_TIP_APPLY_Y) && s_tip_init) {
                const uint8_t ramp_q8 = s_tip_ramp_q8_by_ly[ly];
                tip_ramp_q8 = ramp_q8;

                if (ramp_q8) {
                    const int sh = (int)s_tip_delta_px[lx];

                    /* sh_eff = sh * ramp_q8 / 255  (быстро, q8) */
                    const int sh_eff = (int)(((int32_t)sh * (int32_t)ramp_q8 + 128) >> 8);

                    ly_src = ly - sh_eff;
                    if (ly_src < 0) ly_src = 0;
                    if (ly_src >= FIRE_H) ly_src = FIRE_H - 1;
                }
            }
    #endif


            uint8_t h = s_heat[ly_src][lx];
            if (h == 0) continue;

            uint16_t cx = s_map_cx[ly][lx];
            uint16_t cy = s_map_cy[ly][lx];


            uint8_t r, g, b;
            heat_to_rgb(h, bri, &r, &g, &b);

            #if (FIRE_DEBUG_COLOR_SPLIT == 0)
            #if FIRE_TIP_PROFILE_ENABLE
            #if FIRE_TIP_COLOR_ENABLE
            if (tip_ramp_q8) {
                /* eff: 0..255, масштабируем ramp на максимальную силу tint */
                uint8_t eff = (uint8_t)(((uint16_t)tip_ramp_q8 * (uint16_t)FIRE_TIP_COLOR_MIX_MAX_Q8) >> 8);

                /* target tip color itself is a gradient along ramp: LOW -> HIGH */
                uint8_t tr = u8_lerp((uint8_t)FIRE_TIP_COLOR_LOW_R,  (uint8_t)FIRE_TIP_COLOR_HIGH_R,  tip_ramp_q8);
                uint8_t tg = u8_lerp((uint8_t)FIRE_TIP_COLOR_LOW_G,  (uint8_t)FIRE_TIP_COLOR_HIGH_G,  tip_ramp_q8);
                uint8_t tb = u8_lerp((uint8_t)FIRE_TIP_COLOR_LOW_B,  (uint8_t)FIRE_TIP_COLOR_HIGH_B,  tip_ramp_q8);

                r = u8_lerp(r, tr, eff);
                g = u8_lerp(g, tg, eff);
                b = u8_lerp(b, tb, eff);

            }
            #endif
            #endif
            #endif


            #if FIRE_DEBUG_COLOR_SPLIT
            uint8_t layer = DBG_L_FIELD;


            /* 1) islands: локальные горячие карманы */
            if (s_dbg_island[ly_src][lx]) layer = DBG_L_ISLAND;

            /* 2) jets: подсветить область влияния струи по ширине */
            #if FIRE_JETS_ENABLE
            if (s_jet_life > 0) {
                int jx = (int)(s_jet_x_q8 >> 8);
                int dx = lx - jx;
                if (dx < 0) dx = -dx;
                int d2 = FIRE_W - dx;
                if (d2 < dx) dx = d2;
                if (dx <= (int)s_jet_w) layer = DBG_L_JET;
            }
            #endif

            /* 3) tongues: подсветка основания языков (нижняя зона) */
            for (int i = 0; i < FIRE_TONGUES; i++) {
                const tongue_t *t = &s_tong[i];
                int tx = (int)(t->x_q8 >> 8);
                int dx = lx - tx;
                if (dx < 0) dx = -dx;
                int d2 = FIRE_W - dx;
                if (d2 < dx) dx = d2;
                if (dx <= (int)t->w) {
                    if (ly <= 18) layer = DBG_L_TONGUE; /* ограничиваем по высоте, чтобы не красить весь столб */
                    break;
                }
            }

            /* 4) tip/crown: красим ТОЛЬКО область ramp (чтобы не перекрывать jet/islands сверху) */
            #if FIRE_TIP_PROFILE_ENABLE
            if ((ly >= FIRE_TIP_APPLY_Y) && (ly <= (FIRE_TIP_APPLY_Y + FIRE_TIP_RAMP_H))) {
                layer = DBG_L_TIP;
            }
            #endif

            /* islands должны быть видимы даже в зоне tip-ramp (debug only) */
            if (s_dbg_island[ly_src][lx]) layer = DBG_L_ISLAND;


            /* сохранить яркость пикселя, поменять оттенок */
            uint8_t k = u8_max3(r, g, b);
            dbg_apply_layer(layer, k, &r, &g, &b);
            #endif /* FIRE_DEBUG_COLOR_SPLIT */

            #if FIRE_ISLANDS_ENABLE && FIRE_ISLANDS_WHITE_ENABLE && (FIRE_DEBUG_COLOR_SPLIT == 0)
            /* Visual-only: force islands to look white in normal mode */
            uint8_t im = s_island_mark[ly_src][lx]; /* 0..255 */
            if (im) {
                uint8_t eff = (uint8_t)(((uint32_t)im * (uint32_t)FIRE_ISLANDS_WHITE_MIX_Q8) >> 8);

                /* blend current rgb -> "white", but brightness-aware */
                #if (FIRE_ISLANDS_WHITE_MODE == 1)
                    /* legacy: goes towards full white, can re-brighten at low bri */
                    r = (uint8_t)((uint32_t)r + (((uint32_t)(255 - r) * eff) >> 8));
                    g = (uint8_t)((uint32_t)g + (((uint32_t)(255 - g) * eff) >> 8));
                    b = (uint8_t)((uint32_t)b + (((uint32_t)(255 - b) * eff) >> 8));
                #elif (FIRE_ISLANDS_WHITE_MODE == 2)
                    /* preserve luminance: only shift hue towards gray/white, never brighter */
                    uint8_t k2 = r;
                    if (g > k2) k2 = g;
                    if (b > k2) k2 = b;

                    r = (uint8_t)((uint32_t)r + (((uint32_t)(k2 - r) * eff) >> 8));
                    g = (uint8_t)((uint32_t)g + (((uint32_t)(k2 - g) * eff) >> 8));
                    b = (uint8_t)((uint32_t)b + (((uint32_t)(k2 - b) * eff) >> 8));
                #else /* FIRE_ISLANDS_WHITE_MODE == 3 */
                    /* clamp to global brightness (bri is already 0..255 with floor applied) */
                    uint8_t wb = bri;
                    r = (uint8_t)((uint32_t)r + (((uint32_t)(wb - r) * eff) >> 8));
                    g = (uint8_t)((uint32_t)g + (((uint32_t)(wb - g) * eff) >> 8));
                    b = (uint8_t)((uint32_t)b + (((uint32_t)(wb - b) * eff) >> 8));
                #endif

            }
            #endif


            
            /* Visual-only bottom cooling gradient (body of flame): dim a few bottom rows */
            if (FIRE_VIS_COOL_ROWS > 0 && ly < FIRE_VIS_COOL_ROWS) {
                 int denom = (FIRE_VIS_COOL_ROWS > 1) ? (FIRE_VIS_COOL_ROWS - 1) : 1;

                /* q8: FIRE_VIS_COOL_MIN_Q8 .. 256 (at top of the cooled zone) */
                int32_t q8 = (int32_t)FIRE_VIS_COOL_MIN_Q8
               + (((int32_t)(256 - FIRE_VIS_COOL_MIN_Q8) * (int32_t)ly) / denom);

                r = (uint8_t)(((int32_t)r * q8) >> 8);
                g = (uint8_t)(((int32_t)g * q8) >> 8);
                b = (uint8_t)(((int32_t)b * q8) >> 8);
            }


            /* Write (no additive here; additive reserved for petals/sparks overlays) */
            fx_canvas_set(cx, cy, r, g, b);
        }
    }
}


/* -------------------- Main effect -------------------- */
void fx_fire_render(fx_ctx_t *ctx)
{
    if (!ctx) return;

    // New Time Approach: time comes from master clock (matrix_anim)
    const uint32_t t_ms = ctx->anim_ms;

    // reset when anim time resets (effect switch) or on reboot
    static uint32_t s_last_anim_ms_seen = 0;
    const bool need_reset =
        (!s_inited) ||
        (t_ms == 0u && s_last_anim_ms_seen != 0u) ||
        (t_ms < s_last_anim_ms_seen);

    s_last_anim_ms_seen = t_ms;

    if (need_reset) {
        fire_reset(t_ms);
    }

    // paused = frozen anim time (render still runs)
    const bool paused = (ctx->anim_dt_ms == 0u);

    /* brightness Variant A (+ floor for 0) */
    uint8_t bri = ctx->brightness;
    if (bri == 0) bri = FIRE_BRI_FLOOR0;

    // dt already includes speed scaling (no extra speed_pct here!)
    uint32_t dt_ms = ctx->anim_dt_ms;
    if (dt_ms > FIRE_DT_CAP_MS) dt_ms = FIRE_DT_CAP_MS;

    // keep legacy time bookkeeping (used by fire_reset init paths)
    s_last_ms = t_ms;


    #if FIRE_DEBUG_COLOR_SPLIT
    for (int y = 0; y < FIRE_H; y++) {
    for (int x = 0; x < FIRE_W; x++) s_dbg_island[y][x] = 0;
    }
    #endif


    if (!paused) {
        /* accumulate time and convert to simulation steps */
        s_accum_ms += dt_ms;

        /* ignition ramp 0..255 */
        if (s_ignite_ms < FIRE_IGNITE_MS) {
            s_ignite_ms += dt_ms;
            if (s_ignite_ms > FIRE_IGNITE_MS) s_ignite_ms = FIRE_IGNITE_MS;
        }
        uint8_t ignite_k = (uint8_t)((s_ignite_ms * 255u) / FIRE_IGNITE_MS);

        /* step size (compile-time): smaller ms per step => more steps */
        uint32_t step_ms = (FIRE_BASE_STEP_MS * 100u + (FIRE_SIM_SPEED_PCT / 2u)) / FIRE_SIM_SPEED_PCT;
        if (step_ms < 8u) step_ms = 8u;

        #if FIRE_ISLANDS_ENABLE && FIRE_ISLANDS_WHITE_ENABLE
        if (s_accum_ms >= step_ms) {
            for (int y = 0; y < FIRE_H; y++) {
                for (int x = 0; x < FIRE_W; x++) s_island_mark[y][x] = 0;
            }
        }
        #endif


        while (s_accum_ms >= step_ms) {
            s_accum_ms -= step_ms;

            /* update controls */
            fire_wind_update(step_ms);
            fire_tongues_update(step_ms);
            fire_jets_update();

            /* inject fuel at base */
            fire_inject(ignite_k);

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
            fire_tip_profile_update_q8(tip_raw_q8, dt_ms);
#endif

            /* hot islands */
            fire_islands();

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
