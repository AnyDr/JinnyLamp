# Jinny Lamp — Commands & Dev CLI

## 1) Dev CLI (build/flash/monitor)

Build:
- `idf.py build`
- `idf.py fullclean && idf.py build`

Flash/monitor:
- `idf.py -p COM12 flash`
- `idf.py -p COM12 monitor`

Menuconfig:
- `idf.py -p COM12 menuconfig`

Git checkpoint (в конце сессии):
- `git status`
- `git add -A`
- `git commit -m "<msg>"`
- `git push origin`

---

## 2) Источник истины
Состояние хранится и применяется в лампе (ctrl_bus). Пульт только отправляет команды.
Лампа подтверждает применённое состояние через ESPNOW ACK (snapshot).

`ctrl_state_t`:
- effect_id (uint16)
- brightness (uint8, 0..255)
- speed_pct (uint16, 10..300)
- paused (bool)
- power (bool)
- state_seq (uint32)

---

## 3) ESPNOW команды (j_esn_cmd_t)

1) POWER
- value_u16: 0=off, 1=on
- семантика: off делает safe shutdown (stop/join → DATA=LOW → MOSFET OFF), on включает и возобновляет

2) SET_ANIM
- value_u16: effect_id (uint16)
- требование: effect_id существует в fx_registry лампы

3) SET_PAUSE
- value_u16: 0/1
- семантика: pause замораживает фазу/рендер (без ресета эффекта)

4) SET_BRIGHT
- value_u16: 0..255
- яркость масштабируется софтверно

5) SET_SPEED_PCT
- value_u16: 10..300

6) OTA_START
- перевод лампы в OTA режим (SoftAP + HTTP upload), лампа отправляет OTA info через HELLO extension

---

## 4) ACK (ответ лампы)
ACK всегда содержит snapshot:
- effect_id / brightness / paused / speed_pct / state_seq
