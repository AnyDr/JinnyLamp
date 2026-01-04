#include "j_espnow_link.h"
#include "j_espnow_proto.h"

#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include "j_wifi.h"
#include "ctrl_bus.h"

static const char *TAG = "J_ESPNOW";

static bool parse_hex_byte(const char *s, uint8_t *out)
{
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) return false;
    char tmp[3] = { s[0], s[1], 0 };
    *out = (uint8_t)strtoul(tmp, NULL, 16);
    return true;
}

static bool parse_mac(const char *str, uint8_t mac[6])
{
    // ожидаем "aa:bb:cc:dd:ee:ff"
    if (!str || strlen(str) != 17) return false;
    for (int i = 0; i < 6; i++) {
        const int p = i * 3;
        if (!parse_hex_byte(&str[p], &mac[i])) return false;
        if (i < 5 && str[p + 2] != ':') return false;
    }
    return true;
}

static void send_ack(const uint8_t *dst_mac, uint32_t ack_seq)
{
    ctrl_state_t st = {0};
    ctrl_bus_get_state(&st);

    j_esn_ack_t ack = {0};
    ack.magic     = J_ESN_MAGIC;
    ack.ver       = J_ESN_VER;
    ack.type      = J_ESN_MSG_ACK;
    ack.src_node  = (uint16_t)CONFIG_J_NODE_ID;
    ack.dst_node  = 0; // не критично
    ack.ack_seq   = ack_seq;

    ack.effect_id   = st.effect_id;
    ack.brightness  = st.brightness;
    ack.paused      = st.paused ? 1 : 0;
    ack.speed_pct   = st.speed_pct;
    ack.state_seq   = st.seq;

    (void)esp_now_send(dst_mac, (const uint8_t*)&ack, sizeof(ack));
}

static void apply_ctrl(const j_esn_ctrl_t *m, const uint8_t *src_mac)
{
    // адресация: либо broadcast, либо dst_node совпадает
    if (m->dst_node != 0xFFFFu && m->dst_node != (uint16_t)CONFIG_J_NODE_ID) {
        return;
    }

    ctrl_cmd_t cmd = {0};
    cmd.type = CTRL_CMD_SET_FIELDS;

    switch ((j_esn_cmd_t)m->cmd) {
        case J_ESN_CMD_SET_ANIM:
            cmd.field_mask |= CTRL_F_EFFECT;
            cmd.effect_id = m->value_u16;
            break;

        case J_ESN_CMD_SET_PAUSE:
            cmd.field_mask |= CTRL_F_PAUSED;
            cmd.paused = (m->value_u16 != 0);
            break;

        case J_ESN_CMD_SET_BRIGHT: {
            uint16_t v = m->value_u16;
            if (v > 255) v = 255;
            cmd.field_mask |= CTRL_F_BRIGHTNESS;
            cmd.brightness = (uint8_t)v;
            } break;

        case J_ESN_CMD_SET_SPEED_PCT: {
            uint16_t v = m->value_u16;
            if (v < 10) v = 10;
            if (v > 300) v = 300;
            cmd.field_mask |= CTRL_F_SPEED;
            cmd.speed_pct = v;
            } break;

        case J_ESN_CMD_POWER: {
            // Минимально-инвазивно: делаем "OFF" как paused+brightness=0,
            // "ON" как paused=false и восстановление brightness при необходимости.
            static uint8_t s_last_nonzero_brightness = 128;

            ctrl_state_t st = {0};
            ctrl_bus_get_state(&st);

            const bool on = (m->value_u16 != 0);
            cmd.field_mask |= CTRL_F_PAUSED | CTRL_F_BRIGHTNESS;

            if (!on) {
                if (st.brightness != 0) s_last_nonzero_brightness = st.brightness;
                cmd.paused = true;
                cmd.brightness = 0;
            } else {
                cmd.paused = false;
                cmd.brightness = (st.brightness == 0) ? s_last_nonzero_brightness : st.brightness;
            }
            } break;

        default:
            return;
    }

    (void)ctrl_bus_submit(&cmd);
    send_ack(src_mac, m->seq);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len < (int)sizeof(j_esn_ctrl_t)) return;

    const j_esn_ctrl_t *m = (const j_esn_ctrl_t*)data;
    if (m->magic != J_ESN_MAGIC || m->ver != J_ESN_VER) return;
    if (m->type != J_ESN_MSG_CTRL) return;

    apply_ctrl(m, info->src_addr);
}

#include "esp_wifi_types.h"  // добавь вверху файла, если wifi_tx_info_t не виден

static void on_sent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "send failed");
    }
}


esp_err_t j_espnow_link_start(void)
{
    ESP_ERROR_CHECK(j_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    // Добавляем peer если задан
    if (strlen(CONFIG_J_ESPNOW_PEER_MAC) > 0) {
        uint8_t peer_mac[6] = {0};
        if (!parse_mac(CONFIG_J_ESPNOW_PEER_MAC, peer_mac)) {
            ESP_LOGE(TAG, "Bad peer MAC format: '%s'", CONFIG_J_ESPNOW_PEER_MAC);
            ESP_LOGI(TAG, "CONFIG_J_ESPNOW_PEER_MAC='%s'", CONFIG_J_ESPNOW_PEER_MAC);

            return ESP_ERR_INVALID_ARG;
        }

        esp_now_peer_info_t p = {0};
        memcpy(p.peer_addr, peer_mac, 6);
        p.ifidx = WIFI_IF_STA;
        p.channel = 0;   // 0 => текущий канал (AP или fallback)
        p.encrypt = false;

        esp_err_t err = esp_now_add_peer(&p);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Peer added: %s (ch=%u)", CONFIG_J_ESPNOW_PEER_MAC, (unsigned)j_wifi_get_channel());
    } else {
        ESP_LOGW(TAG, "Peer MAC empty -> no peer added (recv still works, but remote must add us)");
    }

    ESP_LOGI(TAG, "ESPNOW started (node_id=%u, ch=%u)", (unsigned)CONFIG_J_NODE_ID, (unsigned)j_wifi_get_channel());
    return ESP_OK;
}
