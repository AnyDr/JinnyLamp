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
static volatile bool s_starting = false;

// Таймаут OTA: ребут через timeout_s секунд ПОСЛЕ последней активности,
// но никогда не ребутим во время загрузки/прошивки.
static volatile bool     s_uploading = false;
static volatile uint32_t s_last_activity_ms = 0;

static inline void ota_activity_kick(void)
{
    // esp_log_timestamp() -> ms since boot
    s_last_activity_ms = esp_log_timestamp();
}


static esp_err_t uri_get_update(httpd_req_t *req)
{
    const char *html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Jinny OTA</title>"
    "<style>"
    "body{font-family:system-ui,Segoe UI,Arial,sans-serif;margin:24px;max-width:520px}"
    "button{padding:10px 14px;font-size:16px}"
    "progress{width:100%;height:18px}"
    "#st{margin-top:10px;white-space:pre-wrap}"
    "</style></head><body>"
    "<h2>Jinny OTA</h2>"
    "<p>Select firmware (.bin) and upload.</p>"
    "<form id='f'>"
    "<input id='file' type='file' accept='.bin' required><br><br>"
    "<button id='btn' type='submit'>Upload</button>"
    "</form>"
    "<div style='margin-top:14px'>"
    "<progress id='p' max='100' value='0'></progress>"
    "<div id='st'></div>"
    "</div>"
    "<script>"
    "(function(){"
    "const f=document.getElementById('f');"
    "const file=document.getElementById('file');"
    "const p=document.getElementById('p');"
    "const st=document.getElementById('st');"
    "const btn=document.getElementById('btn');"
    "function setStatus(s){st.textContent=s;}"
    "f.addEventListener('submit',function(ev){"
    "ev.preventDefault();"
    "if(!file.files||!file.files.length){setStatus('No file selected');return;}"
    "btn.disabled=true;"
    "p.value=0;"
    "setStatus('Uploading...');"
    "const xhr=new XMLHttpRequest();"
    "xhr.open('POST','/update');"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){"
    "const pc=Math.floor((e.loaded*100)/e.total);"
    "p.value=pc;"
    "setStatus('Uploading... '+pc+'%');"
    "}"
    "};"
    "xhr.onreadystatechange=function(){"
    "if(xhr.readyState===4){"
    "if(xhr.status===200){"
    "p.value=100;"
    "setStatus(xhr.responseText||'OK. Rebooting...');"
    "}else{"
    "setStatus('Error: HTTP '+xhr.status+'\\n'+(xhr.responseText||''));"
    "btn.disabled=false;"
    "}"
    "}"
    "};"
    "xhr.onerror=function(){setStatus('Network error');btn.disabled=false;};"
    "xhr.send(file.files[0]);"
    "});"
    "})();"
    "</script>"
    "</body></html>";



    ota_activity_kick();
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

}

static esp_err_t uri_post_update(httpd_req_t *req)
{
    esp_err_t ret = ESP_FAIL;

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    // Считаем, что начинается попытка OTA. Важно: на всех выходах сбросить s_uploading.
    s_uploading = true;
    ota_activity_kick();

    ESP_LOGI(TAG, "Writing OTA to partition '%s' @0x%lx size=0x%lx",
             part->label, (unsigned long)part->address, (unsigned long)part->size);

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        ret = ESP_FAIL;
        goto out;
    }
    if ((size_t)req->content_len > part->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image too large for OTA slot");
        ret = ESP_FAIL;
        goto out;
    }

    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        ret = err;
        goto out;
    }

    uint8_t buf[1024];
    int remaining = req->content_len;

    while (remaining > 0) {
        int r = httpd_req_recv(req, (char*)buf,
                               (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining);

        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue; // повторяем чтение
        }
        if (r <= 0) {
            (void)esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            ret = ESP_FAIL;
            goto out;
        }

        // Кик активности: во время загрузки это наш “якорь”, чтобы idle-timeout не сработал.
        ota_activity_kick();

        err = esp_ota_write(ota, buf, r);
        if (err != ESP_OK) {
            (void)esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
            ret = err;
            goto out;
        }

        remaining -= r;
    }

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_end failed");
        ret = err;
        goto out;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_set_boot_partition failed");
        ret = err;
        goto out;
    }

    httpd_resp_sendstr(req, "OK. Rebooting...");

    s_uploading = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();

    // Не дойдём, но для формальности:
    return ESP_OK;

out:
    s_uploading = false;
    return ret;
}


static void ota_timeout_task(void *arg)
{
    (void)arg;

    const uint32_t timeout_ms = (uint32_t)s_info.timeout_s * 1000U;

    if (timeout_ms == 0) {
        vTaskDelete(NULL);
        return;
    }


    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!s_running) break;

        // Во время загрузки/прошивки таймаут не срабатывает
        if (s_uploading) continue;

        const uint32_t now = esp_log_timestamp();
        const uint32_t dt  = (uint32_t)(now - s_last_activity_ms);

        if (dt >= timeout_ms) {
            ESP_LOGW(TAG, "OTA portal idle timeout (%u s) -> reboot", (unsigned)s_info.timeout_s);
            ota_portal_stop();
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
    }

    vTaskDelete(NULL);
}


esp_err_t ota_portal_start(const ota_portal_info_t *cfg)
{
    // Если портал уже поднят или поднимается — считаем это повторным входом:
    // продлеваем TTL (activity_kick) и выходим без побочных эффектов.
    if (s_running || s_starting) {
        ota_activity_kick();
        return ESP_OK;
    }

    if (!cfg) return ESP_ERR_INVALID_ARG;

    s_starting = true;

    s_info = *cfg;
    s_uploading = false;
    ota_activity_kick();



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
    s_starting = false;
    ESP_LOGI(TAG, "OTA portal ready: connect to SSID '%s' and open http://192.168.4.1:%u/update",
             s_info.ssid, (unsigned)http_cfg.server_port);

    if (s_info.timeout_s) {
        xTaskCreate(ota_timeout_task, "ota_timeout", 2048, NULL, 5, NULL);
    }

    return ESP_OK;
}

void ota_portal_stop(void)
{
    // stop должен быть идемпотентным, поэтому чистим флаги даже если уже остановлено
    s_starting  = false;
    s_uploading = false;

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
