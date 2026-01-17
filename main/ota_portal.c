#include "ota_portal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "j_wifi.h"
#include "xvf_i2c.h"
#include "ctrl_bus.h"

#include "driver/gpio.h"
#include "matrix_anim.h"
#include "matrix_ws2812.h"


static const char *TAG = "OTA_PORTAL";

static httpd_handle_t s_http = NULL;
static bool s_running = false;
static ota_portal_info_t s_info = {0};

static esp_err_t uri_get_update(httpd_req_t *req)
{
    static const char *html =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>Jinny OTA</title></head><body>"
        "<h3>Jinny Lamp OTA</h3>"
        "<form method='POST' action='/update' enctype='application/octet-stream'>"
        "<p>Select firmware .bin and upload:</p>"
        "<input type='file' id='f' name='f'/>"
        "<button type='button' onclick='up()'>Upload</button>"
        "</form>"
        "<pre id='log'></pre>"
        "<script>"
        "function up(){"
        "let f=document.getElementById('f').files[0];"
        "if(!f){alert('Select .bin');return;}"
        "fetch('/update',{method:'POST',body:f}).then(r=>r.text()).then(t=>{document.getElementById('log').textContent=t;});"
        "}"
        "</script>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t uri_post_update(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing OTA to partition '%s' @0x%lx size=0x%lx",
             part->label, (unsigned long)part->address, (unsigned long)part->size);

        if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
        }
        if ((size_t)req->content_len > part->size) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image too large for OTA slot");
            return ESP_FAIL;
        }


    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return err;
    }

    uint8_t buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, (char*)buf, (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue; // повторяем чтение
        }
        if (r <= 0) {
            (void)esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }


        err = esp_ota_write(ota, buf, r);
        if (err != ESP_OK) {
            esp_ota_end(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
            return err;
        }
        remaining -= r;
    }

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_end failed");
        return err;
    }

    // Базовая проверка заголовка приложения (IDF проверит формат, checksum и т.п.)
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_set_boot_partition failed");
        return err;
    }

    httpd_resp_sendstr(req, "OK. Rebooting...");

    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static void ota_timeout_task(void *arg)
{
    (void)arg;
    const uint32_t t = (uint32_t)s_info.timeout_s;
    for (uint32_t i = 0; i < t; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_running) break;
    }
    if (s_running) {
        ESP_LOGW(TAG, "OTA portal timeout -> stop");
        ota_portal_stop();
    }
    vTaskDelete(NULL);
}

esp_err_t ota_portal_start(const ota_portal_info_t *cfg)
{
    if (s_running) return ESP_OK;
    if (!cfg) return ESP_ERR_INVALID_ARG;

    s_info = *cfg;

    // 1) Тушим свет через ctrl_bus (минимально-инвазивно)
    ctrl_cmd_t cmd = {0};
    cmd.type = CTRL_CMD_SET_FIELDS;
    cmd.field_mask = CTRL_F_PAUSED | CTRL_F_BRIGHTNESS;
    cmd.paused = true;
    cmd.brightness = 0;
    (void)ctrl_bus_submit(&cmd);

    // 2) Останавливаем анимацию и освобождаем WS2812 драйвер, чтобы далее гарантировать DATA=LOW.
    matrix_anim_stop_and_wait(500);
    matrix_ws2812_deinit();

    // 3) DATA=LOW (на случай, если RMT/strip уже остановлен): держим линию в нуле.
    const uint8_t data_gpio = (s_info.data_gpio != 0) ? s_info.data_gpio : 3;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << data_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level((gpio_num_t)data_gpio, 0);

    // 4) MOSFET OFF (через XVF GPO)
    const uint8_t mosfet_pin = (s_info.mosfet_pin != 0) ? s_info.mosfet_pin : 11;
    const uint8_t off_level  = (s_info.mosfet_off_level != 0) ? 1 : 0;
    (void)xvf_gpo_write(mosfet_pin, off_level);


    // 3) Поднимаем SoftAP на текущем канале
    const uint8_t ch = j_wifi_get_channel();
    ESP_ERROR_CHECK(j_wifi_start_softap(s_info.ssid, s_info.pass, ch));

    // 4) HTTP server
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = s_info.port ? s_info.port : 80;
    http_cfg.stack_size = 8192;

    ESP_ERROR_CHECK(httpd_start(&s_http, &http_cfg));

    httpd_uri_t u1 = {.uri="/", .method=HTTP_GET, .handler=uri_get_update, .user_ctx=NULL};
    httpd_uri_t u2 = {.uri="/update", .method=HTTP_GET, .handler=uri_get_update, .user_ctx=NULL};
    httpd_uri_t u3 = {.uri="/update", .method=HTTP_POST, .handler=uri_post_update, .user_ctx=NULL};

    httpd_register_uri_handler(s_http, &u1);
    httpd_register_uri_handler(s_http, &u2);
    httpd_register_uri_handler(s_http, &u3);

    s_running = true;

    ESP_LOGI(TAG, "OTA portal ready: connect to SSID '%s' and open http://192.168.4.1:%u/update",
             s_info.ssid, (unsigned)http_cfg.server_port);

    if (s_info.timeout_s) {
        xTaskCreate(ota_timeout_task, "ota_timeout", 2048, NULL, 5, NULL);
    }

    return ESP_OK;
}

void ota_portal_stop(void)
{
    if (!s_running) return;

    if (s_http) {
        httpd_stop(s_http);
        s_http = NULL;
    }

    (void)j_wifi_stop_softap();
    s_running = false;

    ESP_LOGI(TAG, "OTA portal stopped");
}

bool ota_portal_is_running(void)
{
    return s_running;
}
