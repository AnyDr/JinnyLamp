# HANDOFF — Jinny Lamp (текущее состояние)

## 1) Железо (кратко)
- ReSpeaker XVF3800 + XIAO ESP32-S3
- 3×16×16 WS2812B daisy-chain (48×16)
- DATA: GPIO3 → 74AHCT125(5V) → 330–470Ω → матрицы
- Питание матриц: +5V напрямую, GND через low-side N-MOSFET
- Gate MOSFET: XVF GPO X0D11 (pin=11)

Инвариант WS2812:
- ON: DATA=LOW → MOSFET ON → delay → show
- OFF: stop(join) → DATA=LOW → MOSFET OFF

## 2) Софт / модули
ESP-IDF v5.5.1, Flash 8MB, PSRAM 8MB, OTA layout.

Ключевые модули:
- `matrix_anim.*` — единственный show owner (~10 FPS)
- `matrix_ws2812.*` — framebuffer + show
- `fx_engine.* / fx_registry.* / fx_canvas.*` — эффекты
- `ctrl_bus.*` — источник истины состояния
- `j_espnow_link.* + j_espnow_proto.h` — ESPNOW транспорт/протокол
- `ota_portal.*` — SoftAP + HTTP OTA upload
- `doa_probe.*` — DOA always-on snapshot

## 3) Статус OTA (факт)
- Вход по ESPNOW `OTA_START`
- SoftAP SSID `JINNY-OTA`, gateway `192.168.4.1`
- `/update` принимает `.bin`, пишет OTA slot, reboot по успеху
- timeout 300s

Rollback включён, anti-rollback выключен.

## 4) Статус DOA (факт)
- DOA читается с XVF по I2C: `AEC_AZIMUTH_VALUES` (resid=33 cmd=75 len=16)
- используем `[3] auto-selected beam`, float32 LE radians → degrees
- сервис DOA always-on, debug FX/лог включаются отдельно

## 5) Следующий шаг (если продолжать голос)
- I2S TX (ESP → XVF) + плеер из FS
- wake word + voice state machine
- Genie overlay (LISTEN/SPEAK) внутри одного кадра matrix_anim

## 6) Audio IN (M2) — статус
- Введён `audio_stream` как единый сервис захвата RX:
  - I2S RX читает только `audio_stream_task`
  - выдача потребителям: mono s16 @16kHz через `audio_stream_read_mono_s16()`
- `asr_debug` переведён на чтение из `audio_stream`, без прямого `audio_i2s_read()`
- Подготовлено место для интеграции WakeNet: подключение будет через `audio_stream` API (без конфликтов владения RX)

## Power Management — статус (АКТУАЛЬНО)

Реализована физическая модель питания LED-подсистемы.

### Что сделано
- Введён отдельный модуль `power_management.*`
- Реализован SOFT OFF:
  - stop(join) matrix_anim
  - deinit WS2812
  - DATA=LOW
  - MOSFET OFF
- Реализован корректный выход из SOFT OFF:
  - MOSFET ON
  - задержка
  - WS2812 init
  - restart matrix_anim
- Команда `J_ESN_CMD_POWER` с пульта теперь управляет **реальным питанием**, а не paused/brightness
- Повторные команды ON/OFF безопасны (идемпотентны)
- В SOFT OFF все команды, кроме POWER и OTA_START, игнорируются

### Что сознательно НЕ делаем
- Не кэшируем настройки в SOFT OFF
- Не репортим power_state
- Не принимаем brightness / fx / pause при выключенной лампе

Причина: предсказуемость, отсутствие скрытых состояний, простая серверная интеграция.

### Инвариант
SOFT OFF — это “лампа выключена”.
Чтобы что-то менять — сначала включи.

# HANDOFF — Jinny Lamp (текущее состояние)

## 1) Железо (кратко)
- ReSpeaker XVF3800 + XIAO ESP32-S3
- 3×16×16 WS2812B daisy-chain (48×16)
- DATA: GPIO3 → 74AHCT125(5V) → 330–470Ω → матрицы
- Питание матриц: +5V напрямую, GND через low-side N-MOSFET
- Gate MOSFET: XVF GPO X0D11 (pin=11)

Инвариант WS2812:
- ON: DATA=LOW → MOSFET ON → delay → show
- OFF: stop(join) → DATA=LOW → MOSFET OFF

## 2) Софт / модули
ESP-IDF v5.5.1, Flash 8MB, PSRAM 8MB, OTA layout.

Ключевые модули:
- `matrix_anim.*` — единственный show owner (FPS задаётся в коде)
- `matrix_ws2812.*` — framebuffer + show + управление MOSFET/DATA safe
- `fx_engine.* / fx_registry.* / fx_canvas.*` — эффекты
- `ctrl_bus.*` — источник истины состояния (effect/brightness/speed/paused/state_seq)
- `power_management.*` — SOFT OFF / DEEP SLEEP (stop(join) + LED safe sequencing)
- `j_espnow_link.* + j_espnow_proto.h` — ESPNOW транспорт/протокол
- `ota_portal.*` — SoftAP + HTTP OTA upload
- `doa_probe.*` — DOA always-on snapshot

## 2.1) Power / Off логика (факт)
Состояния:
- **ON**: нормальная работа
- **SOFT OFF**: “выключено” без deep sleep (по пульту/серверу)
- **DEEP SLEEP**: только по локальной сенсорной кнопке

SOFT OFF вход:
- `matrix_anim_stop_and_wait()` (stop=join)
- `matrix_ws2812_data_force_low()`
- `matrix_ws2812_power_set(false)` (MOSFET OFF)
- в SOFT OFF игнорируются прочие ESPNOW команды, кроме `POWER:ON`

SOFT OFF выход:
- `matrix_ws2812_power_set(true)` (MOSFET ON)
- `matrix_ws2812_init(data_gpio)`
- `matrix_anim_start()`

Маппинг источников:
- Remote long power → SOFT OFF/ON
- TTP223 long press → DEEP SLEEP
- Server (план): `off` = SOFT OFF

## 2.2) Pause (факт)
- Пауза **не** останавливает `matrix_anim` и не приводит к перезапуску анимации.
- При `paused=1` рендер пропускается, но `matrix_ws2812_show()` продолжает вызываться с целевым FPS.
- DOA сервис (doa_probe) остаётся живым независимо от паузы; если активный эффект = DOA DEBUG, “заморозка” кадра зависит от политики эффекта/движка.

## 3) Статус OTA (факт)
- Вход по ESPNOW `OTA_START`
- SoftAP SSID `JINNY-OTA`, gateway `192.168.4.1`
- `/update` принимает `.bin`, пишет OTA slot, reboot по успеху
- timeout 300s

Rollback включён, anti-rollback выключен.

## 4) Статус DOA (факт)
- DOA читается с XVF по I2C: `AEC_AZIMUTH_VALUES` (resid=33 cmd=75 len=16)
- используем `[3] auto-selected beam`, float32 LE radians → degrees
- сервис DOA always-on, debug FX/лог включаются отдельно

## 5) Следующий шаг (если продолжать голос)
- I2S TX (ESP → XVF) + плеер из FS
- wake word + voice state machine
- Genie overlay (LISTEN/SPEAK) внутри одного кадра matrix_anim

## 6) Audio IN (M2) — статус
- Введён `audio_stream` как единый сервис захвата RX:
  - I2S RX читает только `audio_stream_task`
  - выдача потребителям: mono s16 @16kHz через `audio_stream_read_mono_s16()`
- `asr_debug` переведён на чтение из `audio_stream`, без прямого `audio_i2s_read()`
- Подготовлено место для интеграции WakeNet: подключение будет через `audio_stream` API (без конфликтов владения RX)

