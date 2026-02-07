#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void genie_overlay_init(void);
void genie_overlay_set_enabled(bool en);
bool genie_overlay_is_enabled(void);

/* Дорисовать оверлей в текущий canvas/буфер. Вызывать из anim task. */
void genie_overlay_render(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
