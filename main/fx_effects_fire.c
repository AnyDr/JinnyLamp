// main/fx_effects_fire.c
#include "fx_engine.h"
#include "fx_canvas.h"

#include <stdint.h>
#include <stdbool.h>

#include "esp_random.h" // esp_random()

// ------------------------------------------------------------
// Физический канвас (как в "последнем файле"): 48x16
// 3 матрицы 16x16 упакованы по X: [0..15][16..31][32..47]
// ------------------------------------------------------------
#define CANVAS_W 48
#define CANVAS_H 16

// ------------------------------------------------------------
// Логическая модель огня: 16 (ширина) x 48 (высота)
// y=0 внизу, y растёт вверх
// ------------------------------------------------------------
#define FLAME_W 16
#define FLAME_H 48

// ------------------------------------------------------------
// Настройки ориентации
// FIRE_ROW_INVERT: инверсия Y внутри каждого 16x16 сегмента
// По твоему наблюдению: при =1 огонь идёт сверху вниз, значит обычно нужно 0.
// ------------------------------------------------------------
#ifndef FIRE_STACK_REVERSE
#define FIRE_STACK_REVERSE 0   // 1 если сегменты по высоте перепутаны местами
#endif

#ifndef FIRE_ROW_INVERT
#define FIRE_ROW_INVERT 0      // 1 если внутри 16x16 нужно инвертировать Y
#endif

// ============================================================
// Helpers
// ============================================================

static inline uint8_t u8_clamp_i32(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline uint8_t scale_u8(uint8_t v, uint8_t scale)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)scale + 127u) / 255u);
}

static inline uint8_t rand8(void)
{
    return (uint8_t)(esp_random() >> 24);
}

static inline uint32_t hash_u32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static inline int wrap_x(int x)
{
    while (x < 0) x += FLAME_W;
    while (x >= FLAME_W) x -= FLAME_W;
    return x;
}

// ============================================================
// State
// ============================================================

// heat[y][x], y=0 bottom
static uint8_t s_heat[FLAME_H][FLAME_W];

// когерентная подпитка основания по колонкам
static uint8_t s_fuel[FLAME_W];

// ветер: q8 (целое *256), плавно меняет направление
static int16_t s_wind_q8 = 0;
static int16_t s_wind_target_q8 = 0;
static uint16_t s_gust_timer = 0;

// сполохи: глобальный коэффициент "всплеска" (0..255) и таймер
static uint8_t  s_flare = 0;
static uint16_t s_flare_timer = 0;

// искры у основания
typedef struct {
    int16_t x_q8;
    int16_t y_q8;
    int16_t vy_q8;
    uint8_t r, g, b;
    uint8_t life; // 0 = мертва
} spark_t;

#define FIRE_SPARKS 8
static spark_t s_sparks[FIRE_SPARKS];

// "языки" (факельный огонь): частицы-потоки
typedef struct {
    int16_t x_q8;
    int16_t y_q8;
    int16_t vx_q8;
    int16_t vy_q8;
    uint8_t heat;     // базовая "температура" языка
    uint8_t width;    // 1..3
    uint8_t life;     // 0 = мёртв
    uint8_t flare_k;  // 0..255 вклад во "внутренний бело-жёлтый сполох"
} plume_t;

#define FIRE_PLUMES 10
static plume_t s_pl[FIRE_PLUMES];

// ============================================================
// Heat access
// ============================================================

static inline uint8_t heat_get(int x, int y)
{
    if (y < 0) y = 0;
    if (y >= FLAME_H) y = FLAME_H - 1;
    x = wrap_x(x);
    return s_heat[y][x];
}

static inline void heat_set(int x, int y, uint8_t v)
{
    if (y < 0 || y >= FLAME_H) return;
    x = wrap_x(x);
    s_heat[y][x] = v;
}

static inline void heat_add(int x, int y, int add)
{
    if (y < 0 || y >= FLAME_H) return;
    x = wrap_x(x);
    int v = (int)s_heat[y][x] + add;
    s_heat[y][x] = u8_clamp_i32(v);
}

// ============================================================
// Map logical (16x48) -> physical canvas (48x16)
// ly=0 bottom, ly increases upward
// ============================================================

static inline void map_to_canvas(int lx, int ly, uint16_t *cx, uint16_t *cy)
{
    int seg = ly / 16;   // 0 bottom, 1 mid, 2 top
    int row = ly % 16;   // 0..15 внутри сегмента (0 ближе к низу логики)

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

// ============================================================
// Palette (огонь по ТЗ: белое только редкими сполохами внутри языков)
// ============================================================

static void heat_to_rgb(uint8_t heat, uint8_t flare_k, uint8_t bri,
                        uint8_t *r, uint8_t *g, uint8_t *b)
{
    // flare_k усиливает "внутренний" жёлто-белый жар, но не у основания ковром
    // flare_k = 0..255, применяется мягко.

    int rr, gg, bb;

    if (heat < 64) {
        // тёмно-красный
        rr = heat * 3;           // 0..192
        gg = heat / 2;           // 0..32
        bb = 0;
    } else if (heat < 160) {
        // красный -> оранжевый -> жёлтый
        int t = heat - 64;       // 0..95
        rr = 192 + t;            // 192..287
        gg = 16 + (t * 2);       // 16..206
        bb = 0;
    } else {
        // верхний жар: жёлтый, белизна только с flare
        int t = heat - 160;      // 0..95
        rr = 255;
        gg = 200 + (t);          // 200..295
        bb = (t / 6);            // 0..15
    }

    // flare: добавляет белизну/жар внутри языка (не снизу линией)
    // усиливаем G и немного B, и слегка поднимаем R (но он и так 255 сверху)
    if (flare_k) {
        int fk = (int)flare_k; // 0..255
        gg += (fk * 40) / 255; // +0..40
        bb += (fk * 60) / 255; // +0..60
        rr += (fk * 10) / 255; // +0..10
    }

    uint8_t R = u8_clamp_i32(rr);
    uint8_t G = u8_clamp_i32(gg);
    uint8_t B = u8_clamp_i32(bb);

    *r = scale_u8(R, bri);
    *g = scale_u8(G, bri);
    *b = scale_u8(B, bri);
}

// ============================================================
// Plumes and sparks
// ============================================================

static void plume_spawn(plume_t *p, uint8_t energy)
{
    p->x_q8 = (int16_t)((esp_random() % FLAME_W) << 8);
    p->y_q8 = 0; // основание: y=0

    // скорость подъёма: ~0.7..1.6 px/frame (q8)
    p->vy_q8 = (int16_t)(180 + (energy * 230u) / 255u);

    // стартовая толщина и "температура"
    p->width = (uint8_t)(1 + (rand8() % 3)); // 1..3
    p->heat  = (uint8_t)(150 + (energy * 90u) / 255u); // 150..240

    // лёгкий собственный дрейф (ветер сделает основное)
    p->vx_q8 = (int16_t)((int8_t)((rand8() % 41) - 20)); // -20..+20

    p->life = (uint8_t)(160 + (energy / 2)); // время жизни
    p->flare_k = 0;
}

static void spark_spawn(uint8_t energy)
{
    // ищем свободный слот
    for (int i = 0; i < FIRE_SPARKS; i++) {
        if (s_sparks[i].life == 0) {
            spark_t *s = &s_sparks[i];

            s->x_q8 = (int16_t)((esp_random() % FLAME_W) << 8);
            s->y_q8 = (int16_t)((rand8() % 3) << 8); // y=0..2

            // искра летит чуть быстрее языка
            s->vy_q8 = (int16_t)(260 + (energy * 120u) / 255u);

            // цвет: синий/голубой/зелёный (редкие)
            uint8_t sel = (uint8_t)(rand8() % 3);
            if (sel == 0) { s->r = 10;  s->g = 40;  s->b = 200; } // blue
            else if (sel == 1) { s->r = 10; s->g = 160; s->b = 200; } // cyan
            else { s->r = 10; s->g = 220; s->b = 40; } // green

            s->life = (uint8_t)(40 + (energy / 10)); // короткая жизнь
            return;
        }
    }
}

// ============================================================
// Effect
// ============================================================

void fx_fire_render(fx_ctx_t *ctx, uint32_t t_ms)
{
    if (!ctx) return;

    const bool paused = ctx->paused;

    // speed_pct 10..300 -> energy 0..255
    uint16_t spd = ctx->speed_pct;
    if (spd < 10)  spd = 10;
    if (spd > 300) spd = 300;
    uint8_t energy = (uint8_t)((spd * 255u) / 300u);

    // локальная яркость эффекта (ограничим, чтобы не вылетало в "стену")
    uint8_t bri = ctx->brightness;
    if (bri > 245) bri = 245;

    if (!paused) {

        // --------------------------------------------------------
        // 1) Ветер: лёгкий, порывистый, меняет направление
        // --------------------------------------------------------
        if (s_gust_timer == 0) {
            // новый порыв раз в ~0.3..1.3 сек (в зависимости от speed)
            uint16_t base = (uint16_t)(6 + (rand8() % 18)); // 6..23
            uint16_t dur = (uint16_t)(base * 50);           // ~300..1150 ms
            // быстрее = чаще порывы
            if (energy > 180 && dur > 350) dur -= 200;

            s_gust_timer = dur;

            // цель ветра: -220..+220 (q8)
            int16_t w = (int16_t)((int16_t)(rand8() % 441) - 220);
            // лёгкий ветерок, не ураган
            s_wind_target_q8 = (int16_t)(w);
        } else {
            // убывает по времени
            uint16_t step = 50; // считаем, что кадры примерно 10 FPS, t_ms всё равно "шумит"
            if (s_gust_timer > step) s_gust_timer -= step;
            else s_gust_timer = 0;
        }

        // плавно тянем текущий ветер к цели
        // 1/8 фильтр
        s_wind_q8 = (int16_t)(s_wind_q8 + ((s_wind_target_q8 - s_wind_q8) / 8));

        // --------------------------------------------------------
        // 2) Сполохи (редкие): усиливают flare на короткое время
        // --------------------------------------------------------
        if (s_flare_timer == 0) {
            // редкая вероятность
            uint8_t p = (uint8_t)(3 + energy / 60); // 3..7
            if (rand8() < p) {
                s_flare_timer = (uint16_t)(250 + (rand8() % 350)); // 250..600 ms
                s_flare = (uint8_t)(140 + (rand8() % 100));        // 140..239
            } else {
                s_flare = 0;
            }
        } else {
            uint16_t step = 50;
            if (s_flare_timer > step) s_flare_timer -= step;
            else s_flare_timer = 0;

            // затухание flare
            if (s_flare > 6) s_flare -= 6;
            else s_flare = 0;
        }

        // --------------------------------------------------------
        // 3) Cooling: мягкое, чтобы пламя занимало большую часть высоты
        // --------------------------------------------------------
        uint8_t cool_base = (uint8_t)(6u - (energy * 4u) / 255u); // ~2..6
        if (cool_base < 2) cool_base = 2;

        for (int y = 0; y < FLAME_H; y++) {
            // сверху немного сильнее, но не душим
            uint8_t cool_y = (uint8_t)(cool_base + (uint8_t)((y * 4u) / FLAME_H)); // +0..+4
            for (int x = 0; x < FLAME_W; x++) {
                int v = (int)s_heat[y][x] - (int)cool_y;
                s_heat[y][x] = u8_clamp_i32(v);
            }
        }

        // --------------------------------------------------------
        // 4) Coherent fuel at base
        // --------------------------------------------------------
        for (int x = 0; x < FLAME_W; x++) {
            uint8_t rnd = rand8();
            s_fuel[x] = (uint8_t)((3u * s_fuel[x] + rnd) >> 2);
        }

        // --------------------------------------------------------
        // 5) Адвекция вверх + ветер (сильнее на высоте)
        // --------------------------------------------------------
        for (int y = FLAME_H - 1; y >= 1; y--) {
            // ветер сильнее вверху: k 40..220 (q8)
            int16_t k = (int16_t)(40 + (y * 180) / (FLAME_H - 1));
            // dx_q8 примерно -190..+190 (лёгкий)
            int16_t dx_q8 = (int16_t)((s_wind_q8 * k) / 256);

            for (int x = 0; x < FLAME_W; x++) {
                // турбулентность - маленькая, локальная
                uint32_t h = hash_u32((uint32_t)x ^ ((uint32_t)y << 8) ^ ((uint32_t)(t_ms / 70) << 16) ^ 0xA6u);
                int8_t turb = (int8_t)((h >> 24) & 0x03); // 0..3
                turb -= 1; // -1..+2 (асимметрия даст "живость")

                // источник берём из строки ниже с учётом ветра
                int xs = x + (dx_q8 >> 8) + turb;
                xs = wrap_x(xs);

                int a  = (int)s_heat[y - 1][xs];
                int al = (int)s_heat[y - 1][wrap_x(xs - 1)];
                int ar = (int)s_heat[y - 1][wrap_x(xs + 1)];

                int adv = (a * 3 + al + ar) / 5;

                int loss = 2;
                if (energy < 80) loss = 3;

                int out = adv - loss;
                if (out < 0) out = 0;

                s_heat[y][x] = (uint8_t)out;
            }
        }

        // --------------------------------------------------------
        // 6) Языки (plumes): факельные "лепестки"
        // --------------------------------------------------------
        // вероятность спавна растёт со speed, но остаётся умеренной
        uint8_t spawn_prob = (uint8_t)(30u + (energy * 90u) / 255u); // 30..120

        // держим минимум живых языков
        int alive_cnt = 0;
        for (int i = 0; i < FIRE_PLUMES; i++) alive_cnt += (s_pl[i].life ? 1 : 0);

        for (int i = 0; i < FIRE_PLUMES; i++) {
            plume_t *p = &s_pl[i];

            if (p->life == 0) {
                if (rand8() < spawn_prob || alive_cnt < 3) {
                    plume_spawn(p, energy);
                    alive_cnt++;
                }
                continue;
            }

            // движение вверх
            p->y_q8 = (int16_t)(p->y_q8 + p->vy_q8);

            // высота в пикселях
            int y = (int)(p->y_q8 >> 8);

            // у факела верх "гуляет": даём лёгкое колыхание по x, усиливающееся с высотой
            int16_t height_k = (int16_t)(30 + (y * 140) / (FLAME_H - 1)); // 30..170
            int16_t wx = (int16_t)((s_wind_q8 * height_k) / 256);

            // локальное колыхание языка
            int8_t wob = (int8_t)((rand8() % 9) - 4); // -4..+4
            p->vx_q8 = (int16_t)(p->vx_q8 + wob);

            // трение
            p->vx_q8 = (int16_t)((p->vx_q8 * 7) / 8);

            // итоговый x
            p->x_q8 = (int16_t)(p->x_q8 + p->vx_q8 + wx);

            int x = (int)(p->x_q8 >> 8);
            x = wrap_x(x);

            // вышли за верх
            if (y >= (FLAME_H - 1)) {
                p->life = 0;
                continue;
            }

            // сполохи внутри языка: редкие, в середине/верхе
            // flare_k растёт кратковременно
            if (s_flare && y > 10 && y < 36) {
                // шанс попасть в язык
                if (rand8() < (uint8_t)(10 + energy / 20)) {
                    uint8_t add = (uint8_t)(s_flare / 3);
                    if (p->flare_k < (uint8_t)(255 - add)) p->flare_k += add;
                    else p->flare_k = 255;
                }
            } else {
                // затухание flare_k
                if (p->flare_k > 6) p->flare_k -= 6;
                else p->flare_k = 0;
            }

            // базовая температура языка + чуть уменьшаем кверху, чтобы верх тончал
            int heat = (int)p->heat - (y / 2);
            if (heat < 40) heat = 40;

            // депозит "лепестком"
            heat_add(x, y, heat);

            if (p->width >= 2) {
                heat_add(x - 1, y, heat / 2);
                heat_add(x + 1, y, heat / 2);
            }
            if (p->width >= 3) {
                heat_add(x - 2, y, heat / 4);
                heat_add(x + 2, y, heat / 4);
            }

            // чуть тела выше/ниже
            heat_add(x, y + 1, heat / 3);
            if (y > 0) heat_add(x, y - 1, heat / 3);

            // расходуем жизнь
            if (p->life > 0) p->life--;
            else p->life = 0;
        }

        // --------------------------------------------------------
        // 7) Основание (y=0): кипящее, красно-оранжевое, без белой линии
        // --------------------------------------------------------
        for (int x = 0; x < FLAME_W; x++) {
            int base   = 55 + (int)((energy * 75u) / 255u);  // 55..130
            int fuel   = (int)(s_fuel[x] >> 1);              // 0..127
            int jitter = (int)(rand8() & 0x3F);              // 0..63

            // небольшая модуляция ветром: ветер сдвигает "центр" источника
            int wind_bias = (int)((s_wind_q8 >> 8) * 2);

            int v = base + fuel + jitter;
            // кап, чтобы низ не уходил в белый
            if (v > 200) v = 200;

            // "дыхание" и неоднородность
            if (rand8() < (uint8_t)(150 + energy / 3)) {
                heat_add(x + wind_bias, 0, v);
            }
        }

        // --------------------------------------------------------
        // 8) Асинхронная верхняя кромка: рваная, 10–15 строк разброс
        // --------------------------------------------------------
        int tip_base = 34 + (energy / 20); // 34..46
        if (tip_base > (FLAME_H - 4)) tip_base = (FLAME_H - 4);

        for (int x = 0; x < FLAME_W; x++) {
            // шум по колонке + влияние ветра, чтобы кромка "плыла"
            uint32_t h = hash_u32((uint32_t)x ^ ((uint32_t)(t_ms / 90) << 16) ^ (uint32_t)(s_wind_q8 << 1));
            int wob = (int)((h >> 24) & 0x0F); // 0..15
            wob -= 7;                           // -7..+8 (это и даёт 10–15 строк разницы)

            int tip = tip_base + wob;
            if (tip < 18) tip = 18;
            if (tip > (FLAME_H - 2)) tip = (FLAME_H - 2);

            // всё выше tip тушим быстро
            for (int y = tip + 1; y < FLAME_H; y++) {
                uint8_t v = s_heat[y][x];
                // сверху тонко и быстро гаснет
                v = (uint8_t)((v * 2u) / 10u); // 0.2
                s_heat[y][x] = v;
            }

            // сама кромка мерцает
            {
                uint8_t v = s_heat[tip][x];
                uint8_t n = (uint8_t)((h >> 16) & 0x1F); // 0..31
                int vv = (int)v - (int)(10 + n);
                s_heat[tip][x] = u8_clamp_i32(vv);
            }
        }

        // --------------------------------------------------------
        // 9) Искры: редкие синие/голубые/зелёные у основания
        // --------------------------------------------------------
        // вероятность небольшая
        uint8_t sp = (uint8_t)(2u + energy / 120u); // 2..4
        if (rand8() < sp) {
            spark_spawn(energy);
        }

        for (int i = 0; i < FIRE_SPARKS; i++) {
            spark_t *s = &s_sparks[i];
            if (s->life == 0) continue;

            s->y_q8 = (int16_t)(s->y_q8 + s->vy_q8);

            int y = (int)(s->y_q8 >> 8);
            int x = (int)(s->x_q8 >> 8);
            x = wrap_x(x);

            // чуть колышем ветром (меньше, чем огонь)
            x = wrap_x(x + (s_wind_q8 >> 10));

            if (y >= FLAME_H) {
                s->life = 0;
                continue;
            }

            // как искра: не нагреваем heat, а рисуем поверх (потом при рендере)
            // тут только уменьшаем жизнь
            if (s->life > 0) s->life--;
            else s->life = 0;
        }

        ctx->frame++;
    }

    // --------------------------------------------------------
    // Render: heat -> RGB -> canvas (через map)
    // --------------------------------------------------------
    for (int ly = 0; ly < FLAME_H; ly++) {
        for (int lx = 0; lx < FLAME_W; lx++) {
            uint8_t r, g, b;

            // flare внутри языков: в этой упрощённой модели берём flare по высоте и общему flare
            // плюс небольшая добавка от локального жара (чтобы не было белой "полосы")
            uint8_t flare_k = 0;
            if (s_flare && ly > 10 && ly < 38) {
                // сильнее в середине огня
                int mid = (ly < 24) ? ly : (48 - ly);
                int fk = (int)s_flare + (mid * 6);
                if (fk > 255) fk = 255;
                flare_k = (uint8_t)fk;
            }

            heat_to_rgb(s_heat[ly][lx], flare_k, bri, &r, &g, &b);

            uint16_t cx, cy;
            map_to_canvas(lx, ly, &cx, &cy);

            if (cx < CANVAS_W && cy < CANVAS_H) {
                fx_canvas_set(cx, cy, r, g, b);
            }
        }
    }

    // --------------------------------------------------------
    // Overlay: искры (синие/голубые/зелёные) поверх огня
    // --------------------------------------------------------
    for (int i = 0; i < FIRE_SPARKS; i++) {
        spark_t *s = &s_sparks[i];
        if (s->life == 0) continue;

        int ly = (int)(s->y_q8 >> 8);
        int lx = wrap_x((int)(s->x_q8 >> 8));

        if (ly < 0 || ly >= FLAME_H) continue;

        uint16_t cx, cy;
        map_to_canvas(lx, ly, &cx, &cy);

        // fade по жизни
        uint8_t fade = s->life; // 1..~50
        uint8_t sr = scale_u8(s->r, (uint8_t)(fade * 5u));
        uint8_t sg = scale_u8(s->g, (uint8_t)(fade * 5u));
        uint8_t sb = scale_u8(s->b, (uint8_t)(fade * 5u));

        // учёт brightness
        sr = scale_u8(sr, bri);
        sg = scale_u8(sg, bri);
        sb = scale_u8(sb, bri);

        if (cx < CANVAS_W && cy < CANVAS_H) {
            fx_canvas_set(cx, cy, sr, sg, sb);
        }
    }

    fx_canvas_present();
}
