#include "ctrl_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "fx_engine.h"
#include "fx_registry.h"

static const char *TAG = "CTRL_BUS";

static QueueHandle_t s_q = NULL;
static TaskHandle_t  s_task = NULL;

// state + защита
static ctrl_state_t s_st = {
    .effect_id   = 0xCA01, // FIRE (default)
    .brightness  = 102,
    .speed_pct   = 100,
    .paused      = false,
    .seq         = 0,
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint8_t clamp_u8(int v, int lo, int hi)
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (uint8_t)v;
}

static uint16_t clamp_u16(int v, int lo, int hi)
{
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (uint16_t)v;
}

static void apply_state_to_engine_locked(void)
{
    // вызывается только из ctrl task, под lock держим коротко
    fx_engine_set_brightness(s_st.brightness);
    fx_engine_set_speed_pct(s_st.speed_pct);
    fx_engine_pause_set(s_st.paused);
    fx_engine_set_effect(s_st.effect_id);
}

static void ctrl_task(void *arg)
{
    (void)arg;

    ctrl_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_q, &cmd, portMAX_DELAY) != pdTRUE) continue;

        bool changed = false;

        portENTER_CRITICAL(&s_lock);

        switch (cmd.type) {
            case CTRL_CMD_SET_FIELDS:
                if (cmd.field_mask & CTRL_F_EFFECT) {
                    s_st.effect_id = cmd.effect_id;
                    changed = true;
                }
                if (cmd.field_mask & CTRL_F_BRIGHTNESS) {
                    s_st.brightness = clamp_u8((int)cmd.brightness, 0, 255);
                    changed = true;
                }
                if (cmd.field_mask & CTRL_F_SPEED) {
                    s_st.speed_pct = clamp_u16((int)cmd.speed_pct, 10, 300);
                    changed = true;
                }
                if (cmd.field_mask & CTRL_F_PAUSED) {
                    s_st.paused = cmd.paused;
                    changed = true;
                }
                break;

            case CTRL_CMD_NEXT_EFFECT: {
                const uint16_t next = fx_registry_next_id(s_st.effect_id);
                s_st.effect_id = next;
                changed = true;
                break;
            }

            case CTRL_CMD_PREV_EFFECT: {
                const uint16_t prev = fx_registry_prev_id(s_st.effect_id);
                s_st.effect_id = prev;
                changed = true;
                break;
            }

            case CTRL_CMD_PAUSE_TOGGLE:
                s_st.paused = !s_st.paused;
                changed = true;
                break;

            case CTRL_CMD_ADJ_BRIGHTNESS: {
                const int v = (int)s_st.brightness + (int)cmd.delta_i8;
                s_st.brightness = clamp_u8(v, 0, 255);
                changed = true;
                break;
            }

            case CTRL_CMD_ADJ_SPEED_PCT: {
                const int v = (int)s_st.speed_pct + (int)cmd.delta_i16;
                s_st.speed_pct = clamp_u16(v, 10, 300);
                changed = true;
                break;
            }

            default:
                break;
        }

        if (changed) {
            s_st.seq++;
        }

        portEXIT_CRITICAL(&s_lock);

        if (changed) {
            apply_state_to_engine_locked();
            ESP_LOGI(TAG, "state: id=%u bri=%u spd=%u%% pause=%u seq=%u",
                     (unsigned)s_st.effect_id,
                     (unsigned)s_st.brightness,
                     (unsigned)s_st.speed_pct,
                     (unsigned)(s_st.paused ? 1 : 0),
                     (unsigned)s_st.seq);
        }
    }
}

esp_err_t ctrl_bus_init(void)
{
    if (s_task) return ESP_OK;

    s_q = xQueueCreate(16, sizeof(ctrl_cmd_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    // init engine + apply default state
    fx_engine_init();
    apply_state_to_engine_locked();

    if (xTaskCreate(ctrl_task, "ctrl_bus", 4096, NULL, 7, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ctrl bus ready");
    return ESP_OK;
}

esp_err_t ctrl_bus_submit(const ctrl_cmd_t *cmd)
{
    if (!cmd) return ESP_ERR_INVALID_ARG;
    if (!s_q) return ESP_ERR_INVALID_STATE;

    if (xQueueSend(s_q, cmd, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void ctrl_bus_get_state(ctrl_state_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_st;
    portEXIT_CRITICAL(&s_lock);
}
