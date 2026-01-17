#pragma once
#include <stdint.h>

/*
 * j_espnow_proto.h (LAMP side)
 *
 * Единый протокол ESPNOW для Jinny Lamp / Remote:
 *  - CTRL : команды управления (power/anim/pause/brightness/speed/ota_start)
 *  - ACK  : подтверждение + snapshot состояния лампы
 *  - HELLO: служебные сообщения (синк списка FX, OTA info для UI)
 *
 * Требования:
 * - Должно компилироваться как C (ESP-IDF) -> НЕ используем "enum : uint8_t".
 * - Все структуры packed: передаём как сырой байтовый буфер через ESPNOW.
 *
 * ВАЖНО по совместимости:
 * - HELLO_OTA_INFO_RSP должен совпадать 1:1 с протоколом пульта:
 *   ota_status + ttl_s + ssid[32+1] + pass[63+1], без port/rsv.
 */

/* =========================
 *  Общие константы протокола
 * ========================= */

#define J_ESN_MAGIC   0x4A4E  /* 'J''N' */
#define J_ESN_VER     1

/* =========================
 *  Типы сообщений (верхний уровень)
 * ========================= */

typedef enum {
    J_ESN_MSG_CTRL  = 1,
    J_ESN_MSG_ACK   = 2,
    J_ESN_MSG_HELLO = 3,
} j_esn_msg_type_t;

/* =========================
 *  Общий заголовок сообщений
 * =========================
 * Первые поля совпадают у CTRL/ACK/HELLO. HELLO-* структуры содержат j_esn_hdr_t
 * целиком. CTRL/ACK ниже дублируют поля, но порядок должен совпадать.
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  ver;
    uint8_t  type;      /* j_esn_msg_type_t */
    uint16_t src_node;
    uint16_t dst_node;  /* 0xFFFF = broadcast */
    uint32_t seq;
} j_esn_hdr_t;

/* =========================
 *  HELLO: команды (подтипы)
 * ========================= */

typedef enum {
    /* FX list sync */
    J_ESN_HELLO_FX_META_REQ   = 1,
    J_ESN_HELLO_FX_META_RSP   = 2,
    J_ESN_HELLO_FX_CHUNK_REQ  = 3,
    J_ESN_HELLO_FX_CHUNK_RSP  = 4,

    /* OTA info (lamp -> remote, send-only) */
    J_ESN_HELLO_OTA_INFO_RSP  = 5,
} j_esn_hello_cmd_t;

/* ============================================================
 *  HELLO: FX LIST SYNC (remote получает список эффектов лампы)
 * ============================================================ */

#define J_ESN_FX_NAME_MAX   16   /* фиксированная длина имени (0-terminated, padded) */
#define J_ESN_FX_CHUNK_MAX  10   /* entries[10] -> влезает в ESPNOW payload */

/* META request: спросить count/crc32 */
typedef struct __attribute__((packed)) {
    j_esn_hdr_t h;         /* type = J_ESN_MSG_HELLO */
    uint8_t  hello_cmd;    /* J_ESN_HELLO_FX_META_REQ */
    uint8_t  rsv0;
    uint16_t rsv1;
    uint32_t rsv2;
} j_esn_fx_meta_req_t;

/* META response: count + crc32 */
typedef struct __attribute__((packed)) {
    j_esn_hdr_t h;         /* type = J_ESN_MSG_HELLO */
    uint8_t  hello_cmd;    /* J_ESN_HELLO_FX_META_RSP */
    uint8_t  rsv0;
    uint16_t fx_count;
    uint32_t fx_crc32;
} j_esn_fx_meta_rsp_t;

/* CHUNK request: стартовый индекс */
typedef struct __attribute__((packed)) {
    j_esn_hdr_t h;         /* type = J_ESN_MSG_HELLO */
    uint8_t  hello_cmd;    /* J_ESN_HELLO_FX_CHUNK_REQ */
    uint8_t  rsv0;
    uint16_t start_index;
    uint32_t rsv1;
} j_esn_fx_chunk_req_t;

/* Одна запись эффекта */
typedef struct __attribute__((packed)) {
    uint16_t id;
    char     name[J_ESN_FX_NAME_MAX];
} j_esn_fx_entry_t;

/* CHUNK response: start_index + count + entries[] */
typedef struct __attribute__((packed)) {
    j_esn_hdr_t h;         /* type = J_ESN_MSG_HELLO */
    uint8_t  hello_cmd;    /* J_ESN_HELLO_FX_CHUNK_RSP */
    uint8_t  count;        /* 1..J_ESN_FX_CHUNK_MAX */
    uint16_t start_index;
    uint32_t fx_crc32;     /* чтобы remote мог отбрасывать “не тот список” */
    j_esn_fx_entry_t entries[J_ESN_FX_CHUNK_MAX];
} j_esn_fx_chunk_rsp_t;

/* ============================================================
 *  HELLO: OTA INFO (lamp -> remote, send-only)
 * ============================================================
 * Контракт пульта:
 *   - status enum: NONE/READY/BUSY/ERROR (0..3)
 *   - ttl_s: сколько секунд данные считать актуальными
 *   - ssid/pass: нул-терминированные строки
 * ВАЖНО: layout должен совпадать с пультом 1:1 (никаких port/rsv полей).
 */

#define J_ESN_OTA_SSID_MAX  32
#define J_ESN_OTA_PASS_MAX  63

typedef enum {
    J_ESN_OTA_ST_NONE  = 0,
    J_ESN_OTA_ST_READY = 1,
    J_ESN_OTA_ST_BUSY  = 2,
    J_ESN_OTA_ST_ERROR = 3,
} j_esn_ota_status_t;

typedef struct __attribute__((packed)) {
    j_esn_hdr_t h;          /* type = J_ESN_MSG_HELLO */
    uint8_t     hello_cmd;  /* J_ESN_HELLO_OTA_INFO_RSP */
    uint8_t     ota_status; /* j_esn_ota_status_t */
    uint16_t    ttl_s;      /* optional, 0 = unknown/unused */
    char        ssid[J_ESN_OTA_SSID_MAX + 1];
    char        pass[J_ESN_OTA_PASS_MAX + 1];
} j_esn_ota_info_rsp_t;

/* =========================
 *  CTRL: команды управления
 * ========================= */

typedef enum {
    J_ESN_CMD_POWER = 1,       /* value_u16: 0=off, 1=on */
    J_ESN_CMD_SET_ANIM,        /* value_u16: effect_id */
    J_ESN_CMD_SET_PAUSE,       /* value_u16: 0/1 */
    J_ESN_CMD_SET_BRIGHT,      /* value_u16: 0..255 */
    J_ESN_CMD_SET_SPEED_PCT,   /* value_u16: 10..300 */
    J_ESN_CMD_OTA_START,       /* value_u16: 0 (reserved) */
} j_esn_cmd_t;

/* CTRL message: первые поля совпадают с j_esn_hdr_t (layout compatibility) */
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  ver;
    uint8_t  type;         /* J_ESN_MSG_CTRL */
    uint16_t src_node;
    uint16_t dst_node;     /* 0xFFFF = broadcast */
    uint32_t seq;

    uint8_t  cmd;          /* j_esn_cmd_t */
    uint8_t  rsv0;
    uint16_t value_u16;

    uint32_t rsv1;
} j_esn_ctrl_t;

/* =========================
 *  ACK: подтверждение + snapshot состояния
 * =========================
 * ACK используется пультом для синхронизации UI и подтверждения команд.
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  ver;
    uint8_t  type;         /* J_ESN_MSG_ACK */
    uint16_t src_node;
    uint16_t dst_node;
    uint32_t ack_seq;

    /* Snapshot состояния (минимум для UI) */
    uint16_t effect_id;
    uint8_t  brightness;
    uint8_t  paused;
    uint16_t speed_pct;
    uint32_t state_seq;
} j_esn_ack_t;
