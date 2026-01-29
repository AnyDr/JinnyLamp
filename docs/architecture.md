# Архитектура Jinny Lamp

Jinny Lamp — прошивка лампы (ESP32-S3 host) на плате ReSpeaker XVF3800 (voice DSP) + WS2812 матрицы.
Лампа — **источник истины** состояния (effect/brightness/speed/paused/power/state_seq). Пульт — клиент: шлёт команды и обновляет UI по ACK.

Каналы управления:
- локально: сенсорная кнопка TTP223 (polling)
- удалённо: ESP-NOW (основной канал)
- OTA: SoftAP + HTTP upload (активируется командой OTA_START по ESP-NOW)
- голос (в работе): WakeNet/MultiNet + fallback на сервер (через bridge)

---

## Инварианты (не ломать)
1) **Single show owner:** только `matrix_anim` вызывает `matrix_ws2812_show()`.
2) **WS2812 power sequencing:**
   - ON: DATA=LOW → MOSFET ON → delay → show
   - OFF: stop(join) → DATA=LOW → MOSFET OFF
3) **OTA safety / power-off / deep sleep:** перед отключениями всегда stop(join) и “LED safe”.
4) **Audio RX single-owner:** `audio_i2s_read()` вызывается только из `audio_stream.c`.
5) **Voice anti-feedback:** во время SPEAK лампа **не слушает** (wake/ASR отключены), чтобы исключить самовозбуждение.
6) **Storage voice policy:** голосовые фразы лежат в data-FS (`storage`) и обновляются **не через OTA**, а по проводу. OTA обновляет только код (app partitions).
7) **I2S state ownership:** состояние I2S (RX/TX/duplex) управляется одним арбитром; повторные enable/disable на уже включённом канале запрещены.

---

## 0) Разметка flash (актуальная)
Цель: “2 OTA слота + отдельный раздел под модель + storage”.

- `ota_0` = 2112 KB
- `ota_1` = 2112 KB
- `model` = 1152 KB  (WakeNet/модели/прочее)
- `storage` = 2688 KB (data partition под FS голосовых файлов)

Смысл:
- OTA остаётся рабочим и безопасным.
- Голосовые файлы не съедают OTA слот, лежат в `storage`.
- Модель WakeNet хранится отдельно (возможна отдельная стратегия обновления позже).

---

## 1) LED pipeline (render → show)
- `matrix_anim` (task, целевой FPS задаётся в `matrix_anim.*`):
  - если `ctrl_bus.paused == 0`: `fx_engine_render(t_ms)` рисует в canvas
  - если `ctrl_bus.paused == 1`: **рендер не делаем**, кадр “заморожен”
  - `matrix_ws2812_show()` вызывается **всегда** (single show owner), чтобы пауза не приводила к “останавливаем/перезапускаем таск”.

Overlay/композиция допустимы только как **post-pass** в рамках одного кадра `matrix_anim` (без второго task/show).

---

## 2) FX engine / registry
- `fx_registry`: таблица эффектов `{id,name,base_step,render_cb}`
- `fx_engine`: держит текущий `effect_id` + параметры; вызывает render с учётом `speed_pct`

Важно: `effect_id` — `uint16` и используется в ESPNOW командах.

---

## 3) Управляющее состояние (ctrl_bus)
`ctrl_bus` — single source of truth:
- effect_id (uint16)
- brightness (uint8, 0..255)
- speed_pct (uint16, 10..300)
- paused (bool)
- state_seq (uint32, монотонный)

Примечание (важно):
- `power` **не является** полем `ctrl_bus` и **не передаётся** в ACK.
- ESPNOW команда `POWER` (ON/OFF) обрабатывается отдельным модулем `power_management`:
  - `OFF` → вход в **SOFT OFF** (stop(join) + LED safe + MOSFET OFF)
  - `ON`  → выход из **SOFT OFF** (MOSFET ON + init WS2812 + старт `matrix_anim`)
- `paused` остаётся логикой “пауза рендера”, не является “выключением питания” и не трогает MOSFET.

Источники команд:
- ESPNOW RX (пульт)
- TTP223 (локально)
- голосовые события (wake/commands/fallback)
- (опционально) серверные команды через bridge

## 3.5) Power management (ON / SOFT OFF / DEEP SLEEP)
Состояния питания **не смешиваются** с `ctrl_bus.paused`.

### SOFT OFF (логический “выключено”, без deep sleep)
Назначение: “как выключить лампу по пульту/серверу”, сохраняя возможность быстро включиться.

Вход (атома, P0):
1) `matrix_anim_stop_and_wait()` (stop=join, без guess-delay)
2) LED safe:
   - `matrix_ws2812_data_force_low()`
   - `matrix_ws2812_power_set(false)` (MOSFET OFF)
3) пометить `power_state = SOFT_OFF`

Выход:
1) `matrix_ws2812_power_set(true)` (MOSFET ON)
2) `matrix_ws2812_init(data_gpio)` + `matrix_anim_start()`
3) `power_state = ON`

Политика команд в SOFT OFF:
- принимаем **только** `POWER:ON` (и, при необходимости, будущие сервисные команды вроде OTA-start, если явно разрешим)
- все остальные ESPNOW команды игнорируются (чтобы “выключено = выключено”)

### DEEP SLEEP
- используется **только** локальной сенсорной кнопкой (TTP223).
- перед уходом: stop(join) + LED safe, затем `esp_deep_sleep_start()`.

### Источники power-команд
- Remote (ESP-NOW): long power → **SOFT OFF / ON**
- Sensor button (TTP223): long press → **DEEP SLEEP**
- Server/bridge (в будущем): “off” = **SOFT OFF**, “on” = выход из SOFT OFF

---

## 4) Wireless политика
NORMAL:
- ESPNOW-only (без STA/MQTT), фиксированный fallback канал (Kconfig)
OTA:
- SoftAP + HTTP OTA портал, вход по ESPNOW `OTA_START`

---

## 5) OTA (SoftAP + HTTP upload)
`ota_portal` владеет жизненным циклом:
- stop anim (join)
- LED safe: DATA=LOW, MOSFET OFF
- SoftAP + httpd `/update`
- запись в OTA partition → set boot partition → reboot
- timeout (напр. 300s) → выход (cleanup/reboot по политике)

Rollback:
- rollback включён, anti-rollback выключен (для dev)

---

## 6) Storage filesystem (актуально: SPIFFS, mount `/spiffs`)
- `storage_fs` монтирует SPIFFS
- голосовые файлы лежат в `/spiffs/voice/...`

---

## 7) DOA
- `doa_probe` читает DOA по I2C с XVF3800 и даёт snapshot (deg + age_ms).
- DOA сервис рассматривается как always-on (даже при паузе рендера); в SOFT OFF/DEEP SLEEP матрица безопасно обесточена.

---

## 8) Voice events (high-level)
- `voice_events` держит маски/события и политику воспроизведения (boot greeting / goodbye и т.д.)
- Плеер и I2S TX ещё доводятся, но архитектурно будет “говорим → не слушаем”.
