#include "j_wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "J_WIFI";

static EventGroupHandle_t s_ev = NULL;
static uint8_t s_channel = 0;          // 0 => неизвестно
static bool    s_connected = false;
static bool s_ap_running = false;


#define WIFI_CONNECTED_BIT  (1u << 0)

static void j_wifi_update_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0) {
        s_channel = primary;
    }
}

static void j_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    (void)arg; (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start");
        if (strlen(CONFIG_J_WIFI_SSID) > 0) {
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else {
            // Нет SSID => работаем в режиме ESPNOW-only на fallback канале
            uint8_t ch = (uint8_t)CONFIG_J_WIFI_FALLBACK_CH;
            ESP_LOGW(TAG, "SSID empty -> ESPNOW-only, set fallback channel=%u", (unsigned)ch);
            ESP_ERROR_CHECK(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
            s_channel = ch;
        }
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "STA connected");
        s_connected = true;
        j_wifi_update_channel();
        xEventGroupSetBits(s_ev, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi channel=%u", (unsigned)s_channel);
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA disconnected");
        s_connected = false;
        xEventGroupClearBits(s_ev, WIFI_CONNECTED_BIT);

        // Если SSID задан, пробуем переподключаться
        if (strlen(CONFIG_J_WIFI_SSID) > 0) {
            esp_wifi_connect();
        } else {
            // SSID пустой => остаёмся на fallback канале
            uint8_t ch = (uint8_t)CONFIG_J_WIFI_FALLBACK_CH;
            ESP_ERROR_CHECK(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
            s_channel = ch;
        }
    }
}

esp_err_t j_wifi_start(void)
{
    if (s_ev != NULL) return ESP_OK;
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();   // нужно для SoftAP OTA портала


    s_ev = xEventGroupCreate();
    if (!s_ev) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &j_wifi_event_handler, NULL));

    wifi_config_t sta_cfg = { 0 };
    if (strlen(CONFIG_J_WIFI_SSID) > 0) {
        strncpy((char*)sta_cfg.sta.ssid, CONFIG_J_WIFI_SSID, sizeof(sta_cfg.sta.ssid));
        strncpy((char*)sta_cfg.sta.password, CONFIG_J_WIFI_PASS, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "esp_wifi_get_mac(WIFI_IF_STA) failed");
    }


    ESP_LOGI(TAG, "WiFi started (ssid_len=%u)", (unsigned)strlen(CONFIG_J_WIFI_SSID));
    return ESP_OK;
}

bool j_wifi_is_connected(void)
{
    return s_connected;
}

uint8_t j_wifi_get_channel(void)
{
    if (s_channel != 0) return s_channel;
    return (uint8_t)CONFIG_J_WIFI_FALLBACK_CH;
}

esp_err_t j_wifi_start_softap(const char *ssid, const char *pass, uint8_t channel)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;

    // Wi-Fi должен быть уже инициализирован через j_wifi_start()
    // Переводим в APSTA, чтобы не ломать внутренние ожидания ESPNOW/STA.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_cfg = { 0 };
    strncpy((char*)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strnlen(ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.channel = channel ? channel : (uint8_t)CONFIG_J_WIFI_FALLBACK_CH;
    ap_cfg.ap.max_connection = 1;

    if (pass && pass[0]) {
        strncpy((char*)ap_cfg.ap.password, pass, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    s_ap_running = true;

    ESP_LOGI(TAG, "SoftAP started: ssid='%s' ch=%u auth=%s",
             ssid, (unsigned)ap_cfg.ap.channel,
             (ap_cfg.ap.authmode == WIFI_AUTH_OPEN) ? "OPEN" : "WPA2");

    return ESP_OK;
}

esp_err_t j_wifi_stop_softap(void)
{
    if (!s_ap_running) return ESP_OK;

    // Самый “тихий” способ: переводим обратно в STA.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    s_ap_running = false;

    ESP_LOGI(TAG, "SoftAP stopped");
    return ESP_OK;
}

bool j_wifi_is_softap_running(void)
{
    return s_ap_running;
}
