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
    { .id = 0,  .name = "SOLID CYCLE",     .base_step = 2,  .render = fx_solid_render },
    { .id = 1,  .name = "RAINBOW",         .base_step = 4,  .render = fx_rainbow_render },
    { .id = 2,  .name = "SPARKLES",        .base_step = 8,  .render = fx_sparkles_render },

    /* Канва-эффекты (пакет #0) */
    { .id = 3,  .name = "MATRIX RAIN",     .base_step = 6,  .render = fx_matrix_rain_render },
    { .id = 4,  .name = "SNOW FALL",       .base_step = 6,  .render = fx_snow_fall_render },

    /* Пакет 1 */
    { .id = 5,  .name = "RAINBOW NOISE",   .base_step = 6,  .render = fx_rainbow_noise_render },
    { .id = 6,  .name = "CLOUD NOISE",     .base_step = 5,  .render = fx_cloud_noise_render },
    { .id = 7,  .name = "LAVA NOISE",      .base_step = 6,  .render = fx_lava_noise_render },
    { .id = 8,  .name = "FOREST NOISE",    .base_step = 5,  .render = fx_forest_noise_render },
    { .id = 9,  .name = "PLASMA NOISE",    .base_step = 7,  .render = fx_plasma_noise_render },

    /* Пакет 2 */
    { .id = 10, .name = "OCEAN NOISE",     .base_step = 5,  .render = fx_ocean_noise_render },
    { .id = 11, .name = "PARTY NOISE",     .base_step = 7,  .render = fx_party_noise_render },
    { .id = 12, .name = "RAINBOW STRIPES", .base_step = 6,  .render = fx_rainbow_stripes_render },
    { .id = 13, .name = "ZEBRA",          .base_step = 5,  .render = fx_zebra_render },
    { .id = 14, .name = "CONFETTI",       .base_step = 8,  .render = fx_confetti_render },

    /* Пакет 3 */
    { .id = 15, .name = "CLOUDS",         .base_step = 6,  .render = fx_clouds_render },
    { .id = 16, .name = "LAVA",           .base_step = 7,  .render = fx_lava_render },
    { .id = 17, .name = "PLASMA",         .base_step = 8,  .render = fx_plasma_render },
    { .id = 18, .name = "FOREST",         .base_step = 5,  .render = fx_forest_render },
    { .id = 19, .name = "OCEAN",          .base_step = 6,  .render = fx_ocean_render },
    /* Пакет 4 */
    { .id = 20, .name = "COLOR WIPE",   .base_step = 6, .render = fx_color_wipe_render   },
    { .id = 21, .name = "LARSON",       .base_step = 6, .render = fx_larson_render       },
    { .id = 22, .name = "BPM BARS",     .base_step = 5, .render = fx_bpm_bars_render     },
    { .id = 23, .name = "TWINKLES",     .base_step = 7, .render = fx_twinkles_render     },
    { .id = 24, .name = "FIRE SIMPLE",  .base_step = 6, .render = fx_fire_simple_render  },
    /* Пакет 5 */
    { .id = 25, .name = "DIAG RAINBOW",     .base_step = 6, .render = fx_diag_rainbow_render     },
    { .id = 26, .name = "RADIAL PULSE",    .base_step = 6, .render = fx_radial_pulse_render     },
    { .id = 27, .name = "CHECKER FLOW",    .base_step = 5, .render = fx_checker_flow_render     },
    { .id = 28, .name = "METEORS",         .base_step = 7, .render = fx_meteors_render          },
    { .id = 29, .name = "GLITTER RAINBOW", .base_step = 6, .render = fx_glitter_rainbow_render  },
    /* Пакет 6 */
    { .id = 30, .name = "V SCANNER",     .base_step = 6, .render = fx_v_scanner_render     },
    { .id = 31, .name = "H SCANNER",     .base_step = 6, .render = fx_h_scanner_render     },
    { .id = 32, .name = "TWINKLE",       .base_step = 6, .render = fx_twinkle_stars_render },
    { .id = 33, .name = "COLOR WAVES",   .base_step = 6, .render = fx_color_waves_render   },
    { .id = 34, .name = "HEAT MAP",      .base_step = 6, .render = fx_heat_map_render      },
    /* Пакет 7 */
    { .id = 35, .name = "CHECKER PULSE",  .base_step = 6, .render = fx_checker_pulse_render      },
    { .id = 36, .name = "DIAG WIPE",      .base_step = 6, .render = fx_diag_rainbow_wipe_render  },
    { .id = 37, .name = "RADIAL RIPPLE",  .base_step = 6, .render = fx_radial_ripple_render      },
    { .id = 38, .name = "FIREFLIES",      .base_step = 7, .render = fx_fireflies_render          },
    { .id = 39, .name = "EQUALIZER",      .base_step = 6, .render = fx_equalizer_bars_render     },
    /* Пакет 8 */
    { .id = 40, .name = "COLOR WAVES X", .base_step = 6, .render = fx_color_waves_x_render },
    { .id = 41, .name = "STROBE SOFT",   .base_step = 9, .render = fx_strobe_soft_render   },
    { .id = 42, .name = "WALKING DOTS",  .base_step = 7, .render = fx_walking_dots_render  },
    { .id = 43, .name = "VORTEX",        .base_step = 6, .render = fx_vortex_render        },
    { .id = 44, .name = "HYPER GRID",    .base_step = 6, .render = fx_hyper_grid_render    },
    /* Пакет 9 */
    { .id = 45, .name = "DIAMOND RINGS",  .base_step = 6, .render = fx_diamond_rings_render  },
    { .id = 46, .name = "COMET DIAGONAL", .base_step = 7, .render = fx_comet_diagonal_render },
    { .id = 47, .name = "FIREWORKS",      .base_step = 8, .render = fx_fireworks_render      },
    { .id = 48, .name = "SCANLINES",      .base_step = 6, .render = fx_scanlines_render      },
    { .id = 49, .name = "CHECKER SHIFT",  .base_step = 6, .render = fx_checker_shift_render  },
    /* Пакет 10 */
    { .id = 50, .name = "AURORA BANDS",   .base_step = 6, .render = fx_aurora_bands_render   },
    { .id = 51, .name = "RADIAL SWIRL",   .base_step = 6, .render = fx_radial_swirl_render   },
    { .id = 52, .name = "METEOR SHOWER",  .base_step = 8, .render = fx_meteor_shower_render  },
    { .id = 53, .name = "KALEIDOSCOPE",   .base_step = 7, .render = fx_kaleidoscope_render   },
    { .id = 54, .name = "RIPPLE FIELD",   .base_step = 6, .render = fx_ripple_field_render   },
    /* Пакет 11 */
    { .id = 55, .name = "FIREFLIES",      .base_step = 6, .render = fx_fireflies_render      },
    { .id = 56, .name = "CHECKER PULSE",  .base_step = 6, .render = fx_checker_pulse_render  },
    { .id = 57, .name = "SPIRAL RIDGES",  .base_step = 7, .render = fx_spiral_ridges_render  },
    { .id = 58, .name = "HEAT SHIMMER",   .base_step = 7, .render = fx_heat_shimmer_render   },
    { .id = 59, .name = "DIAGONAL WAVES", .base_step = 6, .render = fx_diagonal_waves_render },
    /* Пакет 12 */
    { .id = 60, .name = "WHITE_COLOR", .base_step = 2, .render = fx_white_color_render },
    { .id = 61, .name = "COLOR",       .base_step = 3, .render = fx_color_render       },
    { .id = 62, .name = "COLORS",      .base_step = 3, .render = fx_colors_render      },
    { .id = 63, .name = "MADNESS",     .base_step = 6, .render = fx_madness_render     },
    { .id = 64, .name = "BBALLS",      .base_step = 6, .render = fx_bballs_render      },
    /* Пакет 13 */
    { .id = 65, .name = "DNA",       .base_step = 6, .render = fx_dna_render       },
    { .id = 66, .name = "METABALLS", .base_step = 7, .render = fx_metaballs_render },
    { .id = 67, .name = "LAVA LAMP", .base_step = 6, .render = fx_lava_lamp_render },
    { .id = 68, .name = "PRISM",     .base_step = 6, .render = fx_prism_render     },
    { .id = 69, .name = "CUBES",     .base_step = 6, .render = fx_cubes_render     },
    /* Пакет 14 */
    { .id = 70, .name = "TUNNEL",       .base_step = 6, .render = fx_tunnel_render        },
    { .id = 71, .name = "ORBIT DOTS",   .base_step = 7, .render = fx_orbit_dots_render    },
    { .id = 72, .name = "MOIRE",        .base_step = 6, .render = fx_moire_render         },
    { .id = 73, .name = "PARTICLE GRID",.base_step = 6, .render = fx_particle_grid_render },
    /* Индивидуальные */
    { .id = 74, .name = "FIRE", .base_step = 6, .render = fx_fire_render },


    /* Пакет 13 */

    /* Пакет 14 */

    /* Пакет 15 */

    
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
