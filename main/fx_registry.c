#include "fx_registry.h"
#include "fx_engine.h"
#include <stddef.h> // NULL


// Реально портированные эффекты (пока базовые 3, чтобы завести каркас)
void fx_solid_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_sparkles_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_matrix_rain_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_snow_fall_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 1: noise-палитры */
void fx_rainbow_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_cloud_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_lava_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_forest_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_plasma_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 2: ещё 5 портированных эффектов */
void fx_ocean_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_party_noise_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_rainbow_stripes_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_zebra_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_confetti_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 3: ещё 5 портированных эффектов */
void fx_clouds_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_lava_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_plasma_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_forest_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_ocean_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 4: ещё 5 портированных эффектов */
void fx_color_wipe_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_larson_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_bpm_bars_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_twinkles_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_fire_simple_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 5: ещё 5 портированных эффектов */
void fx_diag_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_radial_pulse_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_checker_flow_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_meteors_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_glitter_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 6: ещё 5 портированных эффектов */
void fx_v_scanner_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_h_scanner_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_twinkle_stars_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_color_waves_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_heat_map_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 7: ещё 5 портированных эффектов */
void fx_checker_pulse_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_diag_rainbow_wipe_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_radial_ripple_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_fireflies_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_equalizer_bars_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 8: ещё 5 портированных эффектов */
void fx_color_waves_x_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_strobe_soft_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_walking_dots_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_vortex_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_hyper_grid_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 9: ещё 5 портированных эффектов */
void fx_diamond_rings_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_comet_diagonal_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_fireworks_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_scanlines_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_checker_shift_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 10: ещё 5 портированных эффектов */
void fx_aurora_bands_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_radial_swirl_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_meteor_shower_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_kaleidoscope_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_ripple_field_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 11: ещё 5 портированных эффектов */
void fx_fireflies_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_checker_pulse_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_spiral_ridges_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_heat_shimmer_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_diagonal_waves_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 12: ещё 5 портированных эффектов */
void fx_white_color_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_color_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_colors_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_madness_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_bballs_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 13: ещё 5 портированных эффектов */
void fx_dna_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_metaballs_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_lava_lamp_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_prism_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_cubes_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Пакет 14: ещё 5 портированных эффектов (geo_14) */
void fx_cubes_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_tunnel_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_orbit_dots_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_moire_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_particle_grid_render(fx_ctx_t *ctx, uint32_t t_ms);
/* Индивидуальные */
void fx_fire_render(fx_ctx_t *ctx, uint32_t t_ms);




static const fx_desc_t s_fx[] = {

    

    /* Простые */
    { .id = 4,  .name = "SNOW FALL",       .base_step = 6,  .render = fx_snow_fall_render },
    { .id = 14, .name = "CONFETTI",       .base_step = 8,  .render = fx_confetti_render },
    { .id = 25, .name = "DIAG RAINBOW",   .base_step = 6,  .render = fx_diag_rainbow_render },
    { .id = 29, .name = "GLITTER RAINBOW",.base_step = 6,  .render = fx_glitter_rainbow_render },
    { .id = 37, .name = "RADIAL RIPPLE",  .base_step = 6,  .render = fx_radial_ripple_render },
    { .id = 69, .name = "CUBES",          .base_step = 6,  .render = fx_cubes_render },
    { .id = 71, .name = "ORBIT DOTS",     .base_step = 7,  .render = fx_orbit_dots_render },
    /* Сложные */
    { .id = 74, .name = "FIRE",           .base_step = 6,  .render = fx_fire_render },
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
