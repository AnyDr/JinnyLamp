#include "fx_registry.h"
#include "fx_engine.h"
#include <stddef.h> // NULL


// Реально портированные эффекты (пока базовые 3, чтобы завести каркас)
void fx_solid_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_rainbow_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_sparkles_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_matrix_rain_render(fx_ctx_t *ctx, uint32_t t_ms);
void fx_snow_fall_render(fx_ctx_t *ctx, uint32_t t_ms);


static const fx_desc_t s_fx[] = {
    { .id = 0, .name = "SOLID CYCLE", .base_step = 2,  .render = fx_solid_render      },
    { .id = 1, .name = "RAINBOW",     .base_step = 4,  .render = fx_rainbow_render    },
    { .id = 2, .name = "SPARKLES",    .base_step = 8,  .render = fx_sparkles_render   },

    /* Канва-эффекты (пакет #1) */
    { .id = 3, .name = "MATRIX RAIN", .base_step = 6,  .render = fx_matrix_rain_render },
    { .id = 4, .name = "SNOW FALL",   .base_step = 6,  .render = fx_snow_fall_render   },
};


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
