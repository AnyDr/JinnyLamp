#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_ctx fx_ctx_t;
typedef void (*fx_render_fn_t)(fx_ctx_t *ctx, uint32_t t_ms);

typedef struct {
    uint16_t       id;
    const char    *name;
    uint8_t        base_step;      // базовый шаг фазы на тик при speed=100%
    fx_render_fn_t  render;
} fx_desc_t;

const fx_desc_t *fx_registry_get(uint16_t id);
uint16_t         fx_registry_first_id(void);
uint16_t         fx_registry_next_id(uint16_t cur);
uint16_t         fx_registry_prev_id(uint16_t cur);
uint16_t         fx_registry_count(void);
const fx_desc_t *fx_registry_get_by_index(uint16_t index);
void fx_registry_set_debug_visible(bool enable);
bool fx_registry_is_debug_visible(void);



#ifdef __cplusplus
}
#endif
