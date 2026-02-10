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
   SR модели (WakeNet/MultiNet) лежат в отдельном data-разделе `model` (raw), также обновляются по проводу и не участвуют в OTA.

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

### 0) Разметка flash (актуальная, MultiNet-ready)

Цель: “dual-OTA + отдельный раздел `model` под SR модели + `storage` (SPIFFS) под voice pack”.

Финальная partition table (8MB flash):

- `ota_0` = 1536 KB  @ 0x20000
- `ota_1` = 1536 KB  @ 0x1A0000
- `model` = 2560 KB  @ 0x320000  (WakeNet/MultiNet srmodels.bin, вне OTA)
- `storage` = 2432 KB @ 0x5A0000  (SPIFFS: voice pack и данные, вне OTA)

Смысл:
- OTA обновляет **только код** (ota_0/ota_1).
- SR модели (WakeNet/MultiNet) лежат в `model` и обновляются **по проводу** отдельной прошивкой.
- Голосовые файлы лежат в `storage` (SPIFFS) и обновляются **по проводу**, OTA их не трогает.


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
- `storage_fs` монтирует SPIFFS (partition `storage`)
- voice pack v2 лежит в `/spiffs/v/...` (см. группы `lc/ss/cmd/srv/ota/err`)
- OTA voice policy: voice pack не обновляется по OTA, только по проводу (SPIFFS image flash)


---

## 7) DOA
- `doa_probe` читает DOA по I2C с XVF3800 и даёт snapshot (deg + age_ms).
- DOA сервис рассматривается как always-on (даже при паузе рендера); в SOFT OFF/DEEP SLEEP матрица безопасно обесточена.
- DOA читается с XVF по I2C: `AEC_AZIMUTH_VALUES` (resid=33 cmd=75 len=16)
- используем `[3] auto-selected beam`, float32 LE radians → degrees
- сервис DOA always-on, debug FX/лог включаются отдельно

---
### Wake Session & Voice FSM

В системе реализована концепция *wake-session*:

- Wake-word запускает логическую сессию ожидания команды
- Во время wake-session:
  - активируется визуальный overlay (Genie indicator)
  - воспроизводится голосовое приветствие
- Если команда не поступила за заданный таймаут:
  - wake-session завершается
  - overlay отключается
  - воспроизводится голосовое сообщение об отсутствии команды

Wake-session работает одинаково в состояниях ON и SOFT OFF.
В SOFT OFF отличие только в том, что LED-матрица физически отключена,
а overlay и анимации не отображаются.

## Voice Playback Architecture (v2 — ADPCM over SPIFFS)

### Overview

Jinny Lamp uses a dedicated **voice playback pipeline** for all spoken feedback
(lifecycle, session, commands, OTA, errors).

Starting from **Voice Pack v2**, all voice assets are stored on SPIFFS and played
in **WAV IMA ADPCM (4-bit)** format.

This decision was driven by:
- SPIFFS size constraints
- OTA independence
- deterministic decode cost
- acceptable speech quality at 16 kHz

### Data Flow

SPIFFS (/spiffs/v/...)
↓
audio_player (WAV + IMA ADPCM decoder)
↓
PCM s16 @ 16 kHz (mono)
↓
audio_i2s (TX)
↓
XVF3800 DAC / Amplifier
↓
Speaker

























# Jinny Lamp — Architecture (ESP32-S3 + XVF3800 + WS2812 + Voice)

## 0) System overview

**Jinny Lamp** is a smart lamp built around:
- **ReSpeaker XVF3800** (voice DSP) + integrated **XIAO ESP32-S3** (host MCU)
- **3× WS2812B 16×16 panels** daisy-chained → logical canvas **48×16 (768 px)**
- Audio playback via board amplifier + external speaker (I2S TX from ESP32-S3)
- Control via **ESP-NOW** (Remote) + optional Wi-Fi for OTA portal mode

The system is structured around a few “single source of truth” modules:
- `ctrl_bus` — device state (effect_id / brightness / speed_pct / paused / power / volume, etc.)
- `matrix_anim` — the only animation task (renders + shows frames)
- `audio_*` — I2S init/arb + player + stream extraction for ASR
- `voice_fsm` — coordinates voice session lifecycle and audio responses
- `xvf_i2c` — reads XVF3800 control/status (including DSP-provided signals)

---

## 1) Flash layout / partitions (OTA + models + storage)

Current flash partition table (OTA + models + SPIFFS storage):

- `ota_0` (app) — **1536 KB**
- `ota_1` (app) — **1536 KB**
- `model` (data, subtype 64) — **2560 KB**
- `storage` (data, SPIFFS) — **2432 KB**

### 1.1 What goes where

**`model` partition (raw data)**
- Stores **ESP-SR model bundle** `srmodels.bin` (WakeNet / MultiNet models).
- This partition is *not* updated through OTA slots; it is flashed separately when models change.
- The size is chosen so MultiNet models can fit (srmodels can be ~2.4–4.0 MB depending on selected models/quantization).

**`storage` partition (SPIFFS)**
- Stores **voice pack** (lamp reactions) and other non-OTA assets.
- Updated rarely and flashed “by wire” when needed.

This separation is intentional: app OTA stays small/reliable, while models and voice assets live in stable data partitions.

---

## 2) LED subsystem (WS2812)

Physical:
- 3× 16×16 WS2812B matrices in series (snake layout inside each panel)
- Data from ESP32-S3 via **74AHCT125 @ 5 V** + series resistor 330–470 Ω
- Power: +5 V direct, **GND switched by low-side N-MOSFET**, gate driven by XVF3800 GPO (X0D11)

Critical invariants:
- Always: **DATA=LOW → MOSFET ON → delay → send data**
- Power-down: **stop anim → DATA=LOW → MOSFET OFF**
- Gate pulldown ensures “OFF by default” on reset/boot/floating pin

---

## 3) Audio subsystem (RX/TX) + Player

- I2S RX: microphone/DSP stream (already 16 kHz, 2ch, 24-in-32 in current pipeline)
- For ASR we use **16 kHz mono s16** (downmix/convert is done in the audio stream path)
- I2S TX: playback of voice reactions / prompts
- Player is “no-interrupt”: voice events do not preempt currently playing audio

Important reliability rule:
- Do not start playback before I2S is ready (guard/ready flag is used).

---

## 4) Voice architecture (WakeNet + MultiNet) — target design

### 4.1 Roles
- **WakeNet**: always-on wake word detection while in IDLE.
- **MultiNet**: enabled only for a short **command session** after wake.

### 4.2 Lifecycle (FSM coordinator)
`voice_fsm` is the coordinator. Target states:

- **IDLE**
  - WakeNet ON, MultiNet OFF
- **LISTENING_SESSION**
  - MultiNet ON, WakeNet paused
  - Session timeout (e.g. ~8 s) if no command
- **SPEAKING**
  - ASR is paused (no recognition during playback)
- **POST_GUARD**
  - Short guard (e.g. 300 ms) after playback
  - Then return to LISTENING_SESSION (if session still active) or IDLE

Session exit reasons:
- `CANCEL_SESSION` command
- `SLEEP` command
- timeout “no command”

---

## 5) ASR command routing (“transport”) — intent → action → voice reaction

We separate three concerns:

1) **ASR result** (MultiNet)
   - returns recognized command label (string) + confidence

2) **Intent mapping**
   - recognized label → `asr_cmd_t` enum (stable internal command IDs)

3) **Execution**
   - `asr_cmd_t` triggers:
     - a **physical action** (ctrl_bus/effects/power/volume/etc.)
     - a **voice event** (audio reaction) via `voice_events`

The routing layer must be:
- deterministic (no hidden state)
- device-safe (only lamp local actions)
- non-regressive (does not break existing playback/power invariants)

---

## 6) Storage: voice pack v2 (SPIFFS)

Voice pack v2 directory layout:

`/spiffs/v/{lc,ss,cmd,srv,ota,err}/`

File naming:
`<group>-<event_id>-<variant>.wav` (example: `lc-01-01.wav`)

Format:
- **WAV IMA ADPCM 4-bit**, mono, **16000 Hz**

Notes:
- Voice events do not interrupt current playback (“no-interrupt” policy).
- Some events use shuffle-bag variants; persistent shuffle for BOOT/WAKE/SLEEP is stored in NVS.

---

## 7) OTA mode (SoftAP portal)

- Remote can request lamp to enter OTA mode (ESP-NOW command)
- Lamp starts SoftAP + HTTP update portal for a limited TTL
- During OTA:
  - animation task must stop cleanly (stop=join)
  - WS2812 power invariants are enforced (DATA low, MOSFET off)

---

## 8) Key modules (ESP-IDF)

- `main.c` — init + tasks + lifecycle glue
- `matrix_ws2812.*` — framebuffer + show
- `matrix_anim.c` — animation task (~10 FPS)
- `fx_engine.*`, `fx_registry.c` — effect registry + renderer
- `ctrl_bus.*` — authoritative device state
- `audio_i2s.*`, `audio_player.*`, `audio_stream.*` — I2S + playback + ASR stream (16k mono s16)
- `voice_fsm.*` — voice session coordinator
- `voice_events.*` — event → file path mapping (SPIFFS v2)
- `xvf_i2c.*` — XVF control/status
