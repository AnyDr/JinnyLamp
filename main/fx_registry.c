#include "fx_registry.h"
#include "fx_engine.h"
#include "sdkconfig.h"
#include <stddef.h> // NULL



// Реестр эффектов
void fx_snow_fall_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_confetti_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_diag_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_glitter_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_radial_ripple_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_cubes_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_orbit_dots_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_fire_render(fx_ctx_t *ctx, uint32_t t_ms);
#if CONFIG_J_DOA_DEBUG
void fx_doa_debug_render(fx_ctx_t *ctx, uint32_t t_ms);
#endif





static const fx_desc_t s_fx[] = {
    /* Простые */
    { .id = 0xEA01, .name = "SNOW FALL",        .base_step = 6, .render = fx_snow_fall_render },
    { .id = 0xEA02, .name = "CONFETTI",         .base_step = 8, .render = fx_confetti_render },
    { .id = 0xEA03, .name = "DIAG RAINBOW",     .base_step = 6, .render = fx_diag_rainbow_render },
    { .id = 0xEA04, .name = "GLITTER RAINBOW",  .base_step = 6, .render = fx_glitter_rainbow_render },
    { .id = 0xEA05, .name = "RADIAL RIPPLE",    .base_step = 6, .render = fx_radial_ripple_render },
    { .id = 0xEA06, .name = "CUBES",            .base_step = 6, .render = fx_cubes_render },
    { .id = 0xEA07, .name = "ORBIT DOTS",       .base_step = 7, .render = fx_orbit_dots_render },

    /*Служебные*/
    #if CONFIG_J_DOA_DEBUG
        { .id = 0xED01, .name = "DOA DEBUG",        .base_step = 6, .render = fx_doa_debug_render }, // DOA
    #endif



    /* Сложные */
    { .id = 0xCA01, .name = "FIRE",             .base_step = 6, .render = fx_fire_render },
};



uint16_t fx_registry_count(void)
{
    return (uint16_t)(sizeof(s_fx) / sizeof(s_fx[0]));
}

const fx_desc_t *fx_registry_get_by_index(uint16_t index)
{
    uint16_t n = fx_registry_count();
    if (index >= n) return NULL;
    return &s_fx[index];
}


static int idx_of(uint16_t id)
{
    for (int i = 0; i < (int)(sizeof(s_fx)/sizeof(s_fx[0])); i++) {
        if (s_fx[i].id == id) return i;
    }
    return -1;
}

const fx_desc_t *fx_registry_get(uint16_t id)
{
    const int idx = idx_of(id);
    if (idx < 0) return NULL;
    return &s_fx[idx];
}

uint16_t fx_registry_first_id(void)
{
    return s_fx[0].id;
}

uint16_t fx_registry_next_id(uint16_t cur)
{
    const int idx = idx_of(cur);
    if (idx < 0) return s_fx[0].id;
    const int n = (int)(sizeof(s_fx)/sizeof(s_fx[0]));
    return s_fx[(idx + 1) % n].id;
}

uint16_t fx_registry_prev_id(uint16_t cur)
{
    const int idx = idx_of(cur);
    const int n = (int)(sizeof(s_fx)/sizeof(s_fx[0]));
    if (idx < 0) return s_fx[0].id;
    return s_fx[(idx + n - 1) % n].id;
}
