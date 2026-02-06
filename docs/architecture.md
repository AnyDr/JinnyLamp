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
### FPS policy (актуально)
- Production default: **22 FPS**.
- Причина: `matrix_ws2812_show()` (WS2812 768 LED) занимает ≈22.5 ms и является нижним пределом; при 25 FPS бюджет 40 ms оставляет слишком мало времени для тяжёлых эффектов (например FIRE) без ухудшения качества.

2) **WS2812 power sequencing:**
   - ON: DATA=LOW → MOSFET ON → delay → show
   - OFF: stop(join) → DATA=LOW → MOSFET OFF. “SOFT OFF: LED subsystem OFF. WakeNet + audio_stream + DOA remain active. No LED overlay in SOFT OFF.” “WakeNet + audio_stream + voice responses remain active.”
3) **OTA safety / power-off / deep sleep:** перед отключениями всегда stop(join) и “LED safe”.
4) **Audio RX single-owner:** `audio_i2s_read()` вызывается только из `audio_stream.c`.
5) **Voice anti-feedback:** во время SPEAK лампа **не слушает** (wake/ASR отключены), чтобы исключить самовозбуждение.
6) **Storage voice policy:** голосовые фразы лежат в data-FS (`storage`) и обновляются **не через OTA**, а по проводу. OTA обновляет только код (app partitions).
7) **I2S state ownership:** состояние I2S (RX/TX/duplex) управляется одним арбитром; повторные enable/disable на уже включённом канале запрещены.

## Audio ready invariant
- Любое воспроизведение (audio_player) должно происходить только после того, как I2S TX канал включён.
- Риск: ранний старт play до audio_i2s_init/enable даёт ESP_ERR_INVALID_STATE ("channel not enabled").
- Допустимые решения:
  A) lifecycle gate: флаг audio_ready выставляется после audio_i2s_init+enable, до этого voice events не запускают play.
  B) player self-heal: audio_player делает best-effort включение TX перед первым write (идемпотентно).

### Статус Audio ready invariant (2026-02-02) — DONE
- В `audio_i2s.c` добавлен флаг готовности I2S (`s_i2s_ready`), выставляется в `true` после успешного enable TX+RX.
- В `audio_player.c` возвращён `tx_set_enabled_best_effort(true)` перед стартом проигрывания, чтобы исключить ранний write в disabled TX.
- Механизм tail/flush (тишина после play для предотвращения “вечного писка”) **не изменялся**.

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
  - `matrix_anim` (task, целевой FPS задаётся в `matrix_anim.*`; **production default = 22 FPS**):

Overlay/композиция допустимы только как **post-pass** в рамках одного кадра `matrix_anim` (без второго task/show).

## 1.1 Модель времени анимаций (New Time Approach)

В проекте используется единая модель времени, разделяющая **реальное время** и **время анимации**.

### Master clock
Источник времени — `matrix_anim` (single owner):

- `wall_ms`, `wall_dt_ms`  
  Реальное время (монотонное), не зависит от pause и speed.
- `anim_ms`, `anim_dt_ms`  
  Время анимации:
  - масштабируется `speed_pct`,
  - **замораживается при pause**,
  - **сбрасывается при смене эффекта**.

### Контракт для эффектов (FX)
Каждый эффект получает время только через `fx_engine_render()`:
fx_engine_render(
wall_ms,
wall_dt_ms,
anim_ms,
anim_dt_ms
)

Инварианты:
- эффекты **не считают время сами**,
- `anim_dt_ms` уже включает `speed_pct`,
- повторное масштабирование времени в FX запрещено,
- `wall_*` используется для always-on логики (например DOA).

### Pause semantics
- `pause = 1` ⇒ `anim_dt_ms = 0`
- `wall_*` продолжает идти
- `matrix_ws2812_show()` вызывается всегда
- DOA и сервисные эффекты продолжают жить

Эта модель устраняет зависимость анимаций от FPS и делает pause/скорость детерминированными.

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
- DOA читается с XVF по I2C: `AEC_AZIMUTH_VALUES` (resid=33 cmd=75 len=16)
- используем `[3] auto-selected beam`, float32 LE radians → degrees
- сервис DOA always-on, debug FX/лог включаются отдельно

---
