#include "storage_spiffs.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "STORAGE_FS";

// Монтируем раздел partition_label="storage" в /spiffs
#define STORAGE_BASE_PATH        "/spiffs"
#define STORAGE_PART_LABEL       "storage"

// Важно: НЕ форматировать автоматически (иначе можно “стереть голоса” при любой ошибке монтирования)
#define STORAGE_FORMAT_IF_FAIL   false

// Сколько файлов одновременно может быть открыто (нам хватит мало)
#define STORAGE_MAX_FILES        4

esp_err_t storage_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = STORAGE_PART_LABEL,
        .max_files = STORAGE_MAX_FILES,
        .format_if_mount_failed = STORAGE_FORMAT_IF_FAIL,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spiffs mount failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SPIFFS mounted: label='%s' at %s (max_files=%d)",
             STORAGE_PART_LABEL, STORAGE_BASE_PATH, STORAGE_MAX_FILES);

    return ESP_OK;
}

void storage_spiffs_print_info(void)
{
    size_t total = 0, used = 0;
    esp_err_t err = esp_spiffs_info(STORAGE_PART_LABEL, &total, &used);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spiffs info failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "SPIFFS usage: used=%u bytes, total=%u bytes, free=%u bytes",
             (unsigned)used, (unsigned)total, (unsigned)(total - used));

    // Для SPIFFS важно: при заполнении >~70% могут начинаться деградации GC/скорости.
    // Мы планируем “provisioning-only write”: в рантайме не пишем, только читаем.
    const unsigned pct = (total > 0) ? (unsigned)((used * 100U) / total) : 0U;
    if (pct >= 70U) {
        ESP_LOGW(TAG, "SPIFFS is %u%% full. Expect slower ops if near-full.", pct);
    }
}

void storage_spiffs_list(const char *path, int max_entries)
{
    if (!path) path = STORAGE_BASE_PATH;
    if (max_entries <= 0) max_entries = 32;

    DIR *d = opendir(path);
    if (!d) {
        ESP_LOGW(TAG, "opendir('%s') failed", path);
        return;
    }

    ESP_LOGI(TAG, "List '%s' (max %d):", path, max_entries);

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max_entries) {
                char full[256];

        // Avoid -Wformat-truncation (treated as error): check length before snprintf
        const size_t path_len = strlen(path);
        const size_t name_len = strlen(e->d_name);

        // need: path + '/' + name + '\0'
        const size_t need = path_len + 1u + name_len + 1u;

        const bool can_build_full = (need <= sizeof(full));

        if (can_build_full) {
            // build "path/name" without snprintf to avoid -Wformat-truncation under -Werror
            memcpy(full, path, path_len);
            full[path_len] = '/';
            memcpy(full + path_len + 1u, e->d_name, name_len);
            full[path_len + 1u + name_len] = '\0';
        } else {
            // fallback: copy only entry name (always fits into full[])
            const size_t copy_n = (name_len < (sizeof(full) - 1u)) ? name_len : (sizeof(full) - 1u);
            memcpy(full, e->d_name, copy_n);
            full[copy_n] = '\0';
        }


        struct stat st;
        if (can_build_full && stat(full, &st) == 0) {
            ESP_LOGI(TAG, "  %s  (%u bytes)", full, (unsigned)st.st_size);
        } else {
            // If path was too long (or stat failed) still print the name/path we have
            ESP_LOGI(TAG, "  %s", full);
        }

        n++;
    }

    closedir(d);
}
