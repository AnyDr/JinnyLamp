#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP-IDF core */
#include "esp_log.h"
#include "esp_err.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"


/* Drivers */
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/rtc_io.h"

/* Project modules */
#include "led_control.h"
#include "asr_debug.h"
#include "matrix_anim.h"
#include "audio_stream.h"
#include "audio_i2s.h"
#include "led_control.h"
#include "asr_debug.h"
#include "matrix_anim.h"
#include "storage_spiffs.h"
#include "audio_player.h"
#include "voice_events.h"
#include "power_management.h"
#include "matrix_ws2812.h"
#include "audio_bus.h"
#include "input_ttp223.h"
#include "sense_acs758.h"
#include "ctrl_bus.h"
#include "xvf_i2c.h"
#include "doa_probe.h"
#include "j_wifi.h"
#include "j_espnow_link.h"
#include "voice_fsm.h"
#include "wake_wakenet.h"



// =======================
// GPIO mapping (hardware)
// =======================

// WS2812 DATA (после level-shifter 74AHCT125 -> матрицы)
#define MATRIX_DATA_GPIO         GPIO_NUM_3

// TTP223 touch output (momentary, active-HIGH)
#define TTP223_GPIO              GPIO_NUM_1

// ACS758 analog output -> ADC input (ток через лампу, high-side)
#define ACS758_GPIO              GPIO_NUM_4


// =======================
// XVF3800 I2C + GPO (MOSFET)
// =======================

// I2C к XVF3800 (служебная шина на плате XIAO<->XVF)
#define XVF_I2C_PORT             I2C_NUM_0
#define XVF_I2C_SDA_GPIO         GPIO_NUM_5
#define XVF_I2C_SCL_GPIO         GPIO_NUM_6
#define XVF_I2C_CLK_HZ           400000
#define XVF_I2C_TIMEOUT_MS       50

// У нас MOSFET gate сидит на X0D11.
#define XVF_GPO_MOSFET_PIN       11

// Если вдруг по железу логика инвертирована: 1 = инвертировать
#define XVF_MOSFET_INVERT        0


// =======================
// TTP223 timing (ms)
// =======================

// Отсев дребезга/ложных фронтов (для polling-версии это "стабилизация уровня")
#define TTP_DEBOUNCE_MS          30

// Долгое удержание: генерация TTP223_EVT_LONG
#define TTP_LONG_MS              3000

// Окно ожидания для набора double/triple кликов (после отпускания)
#define TTP_CLICK_GAP_MS         450

// После пробуждения (EXT0 по HIGH): удержание, чтобы считать это "включением"
#define TTP_WAKE_HOLD_MS         2000

// Задержка перед уходом в deep sleep после LONG (после отпускания)
#define TTP_SLEEP_DELAY_MS       500

// Rate-limit для SHORT/DOUBLE/TRIPLE (защита от дублей событий)
#define TTP_ACTION_RATE_MS       150

// Подтверждение LONG (защита от паразитных HIGH)
#define TTP_LONG_CONFIRM_MS      200

// Период опроса "ждём отпускания" в sleep task
#define TTP_RELEASE_POLL_MS      20


// =======================
// WS2812 power sequencing (ms)
// =======================


static const char *TAG = "JINNY_MAIN";


// forward
static void jinny_enter_deep_sleep(void);


// ============================================================
// Deep sleep scheduling
//  - ВАЖНО: уход в сон делаем НЕ из ttp_evt_cb напрямую.
//  - Планируем отдельной задачей, чтобы не блокировать обработку ввода.
// ============================================================

static TaskHandle_t s_sleep_task = NULL;

static void jinny_sleep_task(void *arg)
{
    (void)arg;

    // 1) Ждём отпускания кнопки (чтобы не словить немедленный wake)
    while (gpio_get_level(TTP223_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(TTP_RELEASE_POLL_MS));
    }

    // 2) Отложенный сон (после отпускания)
    ESP_LOGI(TAG, "Deep sleep in %u ms", (unsigned)TTP_SLEEP_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(TTP_SLEEP_DELAY_MS));

    // 3) Реальный уход в сон
    jinny_enter_deep_sleep();

    // Обычно сюда не возвращаемся
    s_sleep_task = NULL;
    vTaskDelete(NULL);
}

static void jinny_schedule_deep_sleep(void)
{
    if (s_sleep_task) {
        // Уже запланировано
        return;
    }

    ESP_LOGI(TAG, "Deep sleep scheduled in %u ms", (unsigned)TTP_SLEEP_DELAY_MS);

    if (xTaskCreate(jinny_sleep_task, "jinny_sleep", 4096, NULL, 5, &s_sleep_task) != pdPASS) {
        s_sleep_task = NULL;
        ESP_LOGE(TAG, "Failed to create sleep task");
    }
}

// ============================================================
// Deep sleep entry / wake guard
// ============================================================

static void jinny_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep...");

    // 1) Остановить анимации (stop=join)
    matrix_anim_stop_and_wait();


    power_mgmt_ws2812_data_force_low();


    // 3) Выключить LED матрицы (железный слой)
    power_mgmt_led_power_off_prepare();


    // 4) Настроить wake по touch (HIGH)
    if (!rtc_gpio_is_valid_gpio(TTP223_GPIO)) {
        ESP_LOGE(TAG, "TTP223_GPIO=%d is not RTC GPIO, cannot use EXT0 wake", (int)TTP223_GPIO);
    } else {
        ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(TTP223_GPIO, 1));
    }

    esp_deep_sleep_start();
}

static bool jinny_boot_wake_guard(void)
{
    // true => продолжаем обычный старт
    // false => сразу обратно в deep sleep
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        return true; // холодный старт или другой источник
    }

    // Для включения нужно удержание
    if (gpio_get_level(TTP223_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(TTP_WAKE_HOLD_MS));
        if (gpio_get_level(TTP223_GPIO) == 1) {
            ESP_LOGI(TAG, "Wake-hold OK, continue boot");
            return true;
        }
    }

    ESP_LOGI(TAG, "Wake tap too short, go back to sleep");
    return false;
}


// ============================================================
// TTP223 event callback (from input_ttp223 polling task)
// ============================================================

static void ttp_evt_cb(ttp223_evt_t evt, void *user)
{
    (void)user;

    // Rate-limit только для SHORT/DOUBLE/TRIPLE, чтобы отсечь "дубли" событий.
    static uint32_t s_last_action_ms = 0;
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    switch (evt) {
        case TTP223_EVT_SHORT: {
            if ((now_ms - s_last_action_ms) < TTP_ACTION_RATE_MS) break;
            s_last_action_ms = now_ms;

            const ctrl_cmd_t c = { .type = CTRL_CMD_PAUSE_TOGGLE };
            (void)ctrl_bus_submit(&c);
            break;
        }

        case TTP223_EVT_DOUBLE: {
            if ((now_ms - s_last_action_ms) < TTP_ACTION_RATE_MS) break;
            s_last_action_ms = now_ms;

            const ctrl_cmd_t c = { .type = CTRL_CMD_NEXT_EFFECT };
            (void)ctrl_bus_submit(&c);
            break;
        }

        case TTP223_EVT_TRIPLE: {
            if ((now_ms - s_last_action_ms) < TTP_ACTION_RATE_MS) break;
            s_last_action_ms = now_ms;

            const ctrl_cmd_t c = { .type = CTRL_CMD_PREV_EFFECT };
            (void)ctrl_bus_submit(&c);
            break;
        }

        case TTP223_EVT_LONG: {
            // Защита от ложных LONG: подтверждаем, что удержание реальное.
            ESP_LOGI(TAG, "APP LONG received (enter confirm)");

            if (gpio_get_level(TTP223_GPIO) == 1) {
                vTaskDelay(pdMS_TO_TICKS(TTP_LONG_CONFIRM_MS));
            }

            if (gpio_get_level(TTP223_GPIO) != 1) {
                ESP_LOGW(TAG, "LONG rejected (not held). Ignoring.");
                break;
            }

            // НЕ ждём отпускания здесь: это сделает sleep-task.
            jinny_schedule_deep_sleep();
            break;
        }

        default:
            break;
    }
}


// ============================================================
// OTA: mark app valid after pending verify
// ============================================================

static void jinny_ota_mark_valid_task(void *arg)
{
    (void)arg;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;

    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK) {
        if (st == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "OTA: pending verify -> will mark valid in 5s if system stays alive");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGW(TAG, "OTA: mark valid result: %s", esp_err_to_name(err));
        }
    }

    vTaskDelete(NULL);
}


// ============================================================
// app_main
// ============================================================

void app_main(void)
{
    ESP_LOGI(TAG, "Jinny lamp starting...");

    ESP_LOGI("PSRAM", "initialized=%d size=%u free_spiram=%u",
             esp_psram_is_initialized(),
             (unsigned)esp_psram_get_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // NVS (needed for persisted audio volume)
    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nerr);
    }


    ESP_ERROR_CHECK(led_control_init());

    // WiFi + ESPNOW (не блокирует старт; SSID может быть пустым)
    ESP_ERROR_CHECK(j_wifi_start());
    ESP_ERROR_CHECK(j_espnow_link_start());

    // XVF3800 control I2C (addr 0x2C)
    const xvf_i2c_cfg_t xvf_cfg = {
        .port = XVF_I2C_PORT,
        .addr_7bit = XVF_I2C_ADDR_DEFAULT,
        .timeout_ticks = pdMS_TO_TICKS(XVF_I2C_TIMEOUT_MS),
    };
    ESP_ERROR_CHECK(xvf_i2c_init(&xvf_cfg, XVF_I2C_SDA_GPIO, XVF_I2C_SCL_GPIO, XVF_I2C_CLK_HZ));
    power_mgmt_init(MATRIX_DATA_GPIO, XVF_GPO_MOSFET_PIN, (XVF_MOSFET_INVERT != 0));


    // Инициализируем кнопку как можно раньше (для wake guard)
    const ttp223_cfg_t tcfg = {
        .gpio = TTP223_GPIO,
        .debounce_ms = TTP_DEBOUNCE_MS,
        .long_press_ms = TTP_LONG_MS,
        .click_gap_ms = TTP_CLICK_GAP_MS,
    };
    ESP_ERROR_CHECK(input_ttp223_init(&tcfg, ttp_evt_cb, NULL));

    // Control bus + FX engine (нужен до обработок команд)
    ESP_ERROR_CHECK(ctrl_bus_init());

    // Если проснулись коротким тапом — обратно в сон
    if (!jinny_boot_wake_guard()) {
        jinny_enter_deep_sleep();
        return;
    }

    // Storage FS (SPIFFS) — монтируем только если реально остаёмся бодрствовать
    ESP_ERROR_CHECK(storage_spiffs_init());
    ESP_ERROR_CHECK(audio_player_init());
    ESP_ERROR_CHECK(audio_bus_init());       // volume load + apply + task start
    ESP_ERROR_CHECK(voice_events_init());
    ESP_ERROR_CHECK(voice_fsm_init());

    

    storage_spiffs_print_info();
    storage_spiffs_list("/spiffs", 32);


    // Датчик тока
    ESP_ERROR_CHECK(acs758_init(ACS758_GPIO));

    ESP_LOGI(TAG, "Audio bus ready (volume=%u)", (unsigned)audio_player_get_volume_pct());

    // ВАЖНО: I2S поднимаем до audio_stream_start(), т.к. stream-task владеет I2S RX
    ESP_ERROR_CHECK(audio_i2s_init());

    // Единый сервис захвата аудио (I2S RX читает только он)
    ESP_ERROR_CHECK(audio_stream_start());

    // Debug читает только из audio_stream (ringbuffer), не владеет I2S RX
    asr_debug_start();
    // Ждём завершения one-shot калибровки, но не бесконечно
    for (int i = 0; i < 60; i++) { // 60 * 50ms = 3000ms
        if (asr_debug_is_cal_done()) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }


    // WS2812 power sequencing (moved to power_management):
    const esp_err_t pwr_err = power_mgmt_led_power_on_prepare();
    if (pwr_err == ESP_OK) {
        ESP_LOGI(TAG, "Starting matrix WS2812 on GPIO=%d", MATRIX_DATA_GPIO);
        ESP_ERROR_CHECK(matrix_ws2812_init(MATRIX_DATA_GPIO));

        ESP_LOGI(TAG, "Starting matrix ANIM");
        matrix_anim_start();
    }

    

    //
    esp_err_t wn_err = wake_wakenet_init();
    if (wn_err != ESP_OK) {
        ESP_LOGE("JINNY_MAIN", "WakeNet disabled: %s", esp_err_to_name(wn_err));
        /* Важно: не abort. Просто продолжаем без wake. */
    }
    //

    //Wake Up word
    ESP_ERROR_CHECK(wake_wakenet_task_start());

    // DOA: XVF иногда не готов сразу после boot/flash.
    // Дадим XVF/I2C немного “прогреться”, чтобы не ловить стартовый timeout.
    vTaskDelay(pdMS_TO_TICKS(800));

    doa_probe_start();  // DOA must run always (data for other components)


    ESP_LOGI(TAG, "System started");

    // Voice Events (real event placeholder):
    voice_event_post(VOICE_EVT_BOOT_GREETING);




    xTaskCreate(jinny_ota_mark_valid_task, "ota_mark_valid", 3072, NULL, 4, NULL);
}
