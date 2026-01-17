# ESP-NOW в Jinny Lamp

Документ про ESP-NOW: pairing, канал, форматы сообщений, команды/ACK, OTA-расширения.

## 1) Назначение
ESP-NOW — основной локальный канал управления лампой с пульта:
- POWER
- PAUSE
- BRIGHTNESS (0..255)
- SPEED_PCT (10..300)
- SET_ANIM (effect_id)
- OTA_START (запуск OTA режима лампы)

Источник истины состояния — лампа. Пульт отправляет команду, лампа отвечает ACK со snapshot состояния.

## 2) Pairing: MAC + node_id (факты текущей связки)
Pairing делается по MAC (через menuconfig/Kconfig).
Фактические MAC (WiFi STA MAC):
- Remote: 98:88:E0:03:D5:F4
- Lamp:   10:B4:1D:EA:DF:C8

Node ID:
- Remote: CONFIG_J_NODE_ID = 2
- Lamp:   CONFIG_J_NODE_ID = 111

## 3) Канал (критично)
ESPNOW работает на текущем канале Wi-Fi радиомодуля.
Текущая политика проекта:
- NORMAL: ESPNOW-only, фиксированный fallback channel (сейчас ch=1)
- OTA: лампа поднимает SoftAP для загрузки .bin; ESPNOW используется для старта OTA и для информирования пульта.

Важно на будущее:
- если когда-либо включим STA к роутеру, ESPNOW обязан жить на канале AP.

## 4) Формат сообщений (в терминах j_espnow_proto.h)
### 4.1 CTRL (команда)
Поля (логически):
- magic/version/type
- src_node, dst_node
- seq
- cmd
- value_u16

### 4.2 ACK (ответ лампы)
ACK подтверждает `ack_seq` и возвращает snapshot состояния:
- effect_id
- brightness
- speed_pct
- paused
- power (если используется)
- state_seq

## 5) Команды (семантика)
- POWER: value_u16 = 0/1
- SET_PAUSE: value_u16 = 0/1
- SET_BRIGHT: value_u16 = 0..255
- SET_SPEED_PCT: value_u16 = 10..300
- SET_ANIM: value_u16 = effect_id
- OTA_START: value_u16 = (0/1 или 0; значение неважно, важен сам cmd)

## 6) OTA-расширение: OTA_INFO (Lamp → Remote)
После OTA_START лампа отправляет на пульт информацию для входа в OTA портал.
Полезная нагрузка (логически):
- ota_status (enum)
- ttl_s
- ssid[33]
- pass[64]

Требования по надёжности:
- допустим 2–3 повтора отправки OTA_INFO (потери пакетов возможны)
- адресация по dst_node = Remote

## 7) Диагностика
Если “ничего не работает”:
1) Проверить MAC pairing на обеих сторонах
2) Проверить node_id src/dst
3) Проверить канал (fallback ch=1) и что обе стороны действительно на одном канале
4) По логам смотреть `J_ESPNOW` и `ESPNOW` init/peer/recv/send

## 8) Известное ограничение
SET_ANIM корректен только если effect_id на пульте совпадает с fx_registry лампы
(или будет протокол синхронизации списка эффектов).

## 9) ESP-IDF v5.x API note (важно для сборки)
В ESP-IDF v5.x send callback ESPNOW использует тип `wifi_tx_info_t` (не `const uint8_t *mac_addr` как в старых версиях).
При ошибке несовместимого типа на `esp_now_register_send_cb()` — сигнатура callback устарела.

