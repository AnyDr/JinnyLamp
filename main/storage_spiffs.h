#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t storage_spiffs_init(void);
void      storage_spiffs_print_info(void);
void      storage_spiffs_list(const char *path, int max_entries);

#ifdef __cplusplus
}
#endif
