#include "fx_registry.h"
#include "fx_engine.h"

#include <stdbool.h>
#include <stddef.h> // NULL

/*
 * FX Registry
 *
 * Цель:
 * - Хранить единый список эффектов (ID/имя/render).
 * - Поддерживать перебор (count/by_index/first/next/prev).
 *
 * DOA DEBUG:
 * - Эффект физически присутствует в массиве, но может быть "невидимым" для UI/перебора.
 * - Видимость определяется функцией j_doa_debug_ui_enabled(), которая живёт в fx_effects_doa_debug.c
 *   и должна управляться одним define в этом файле (single source of truth).
 *
 * Важно:
 * - Legacy API fx_registry_set_debug_visible() сохранён как NOP для совместимости.
 */


/* ------------------------------ Forward declarations ------------------------------ */

// Simple FX
void fx_snow_fall_render(fx_ctx_t *ctx);
void fx_confetti_render(fx_ctx_t *ctx);
void fx_diag_rainbow_render(fx_ctx_t *ctx);
void fx_glitter_rainbow_render(fx_ctx_t *ctx);
void fx_radial_ripple_render(fx_ctx_t *ctx);
void fx_cubes_render(fx_ctx_t *ctx);
void fx_orbit_dots_render(fx_ctx_t *ctx);

// Complex FX
void fx_fire_render(fx_ctx_t *ctx);

// Debug / Service FX
void fx_doa_debug_render(fx_ctx_t *ctx);


// Debug UI toggle
bool j_doa_debug_ui_enabled(void);



/* ------------------------------ Registry table ------------------------------ */

static const fx_desc_t s_fx[] = {
    /* Simple */
    { .id = 0xEA01, .name = "SNOW FALL",        .render = fx_snow_fall_render },
    { .id = 0xEA02, .name = "CONFETTI",         .render = fx_confetti_render },
    { .id = 0xEA03, .name = "DIAG RAINBOW",     .render = fx_diag_rainbow_render },
    { .id = 0xEA04, .name = "GLITTER RAINBOW",  .render = fx_glitter_rainbow_render },
    { .id = 0xEA05, .name = "RADIAL RIPPLE",    .render = fx_radial_ripple_render },
    { .id = 0xEA06, .name = "CUBES",            .render = fx_cubes_render },
    { .id = 0xEA07, .name = "ORBIT DOTS",       .render = fx_orbit_dots_render },

    /* Service / Debug (hidden unless enabled) */
    { .id = 0xED01, .name = "DOA DEBUG",        .render = fx_doa_debug_render },
    


    /* Complex */
    { .id = 0xCA01, .name = "FIRE",             .render = fx_fire_render },
};



/* ------------------------------ Internal helpers ------------------------------ */

static bool is_hidden(const fx_desc_t *d)
{
    // DOA DEBUG: hidden unless enabled by define-driven function in fx_effects_doa_debug.c
    if (d->id == 0xED01u) {
        if (!j_doa_debug_ui_enabled()) return true;
    }
    return false;
}

static int idx_of(uint16_t id)
{
    for (int i = 0; i < (int)(sizeof(s_fx) / sizeof(s_fx[0])); i++) {
        if (s_fx[i].id == id) return i;
    }
    return -1;
}


/* ------------------------------ Public API ------------------------------ */

void fx_registry_set_debug_visible(bool enable)
{
    // Legacy API: kept for compatibility.
    // Debug visibility is controlled solely by j_doa_debug_ui_enabled().
    (void)enable;
}

bool fx_registry_is_debug_visible(void)
{
    return j_doa_debug_ui_enabled();
}

uint16_t fx_registry_count(void)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < (uint16_t)(sizeof(s_fx) / sizeof(s_fx[0])); i++) {
        if (!is_hidden(&s_fx[i])) n++;
    }
    return n;
}

const fx_desc_t *fx_registry_get_by_index(uint16_t index)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < (uint16_t)(sizeof(s_fx) / sizeof(s_fx[0])); i++) {
        if (is_hidden(&s_fx[i])) continue;
        if (n == index) return &s_fx[i];
        n++;
    }
    return NULL;
}

const fx_desc_t *fx_registry_get(uint16_t id)
{
    const int idx = idx_of(id);
    if (idx < 0) return NULL;
    return &s_fx[idx];
}

uint16_t fx_registry_first_id(void)
{
    for (uint16_t i = 0; i < (uint16_t)(sizeof(s_fx) / sizeof(s_fx[0])); i++) {
        if (!is_hidden(&s_fx[i])) return s_fx[i].id;
    }
    // fallback (should never happen)
    return s_fx[0].id;
}

uint16_t fx_registry_next_id(uint16_t cur)
{
    const int n = (int)(sizeof(s_fx) / sizeof(s_fx[0]));
    int idx = idx_of(cur);
    if (idx < 0) idx = 0;

    for (int step = 1; step <= n; step++) {
        const fx_desc_t *d = &s_fx[(idx + step) % n];
        if (!is_hidden(d)) return d->id;
    }

    // fallback (should never happen)
    return s_fx[(idx + 1) % n].id;
}

uint16_t fx_registry_prev_id(uint16_t cur)
{
    const int n = (int)(sizeof(s_fx) / sizeof(s_fx[0]));
    int idx = idx_of(cur);
    if (idx < 0) idx = 0;

    for (int step = 1; step <= n; step++) {
        const fx_desc_t *d = &s_fx[(idx + n - step) % n];
        if (!is_hidden(d)) return d->id;
    }

    // fallback (should never happen)
    return s_fx[(idx + n - 1) % n].id;
}
