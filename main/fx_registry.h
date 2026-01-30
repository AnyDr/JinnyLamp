#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "fx_engine.h"  // fx_ctx_t

typedef void (*fx_render_fn_t)(fx_ctx_t *ctx);


typedef struct fx_desc_t {
    uint16_t      id;
    const char   *name;
    fx_render_fn_t render;
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
