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

## Audio Volume (status)
- На стороне лампы реализовано управление громкостью 0..100:
  - ESPNOW: J_ESN_CMD_SET_AUDIO_VOLUME, J_ESN_CMD_GET_AUDIO_STATE
  - ответ: J_ESN_HELLO_AUDIO_STATE_RSP (volume_pct + state_seq)
- Громкость сохраняется в NVS (namespace "jinny", key "audio_vol") с debounce (~800 ms) и восстанавливается на boot.
- В SOFT OFF громкость/GET_AUDIO_STATE сейчас разрешены (оставляем как есть).
- На стороне пульта осталось: UI + отправка команд + обработка HELLO_AUDIO_STATE_RSP.

## Voice boot greeting (status)
- Реализовано однократное случайное проигрывание boot greeting из 3 файлов /spiffs/voice/boot_greeting_{1..3}.pcm
- Выбор без повторов через persistent played_mask (NVS).

## Soak result (50 plays)
- Серия 50 воспроизведений подряд в установившемся режиме проходит стабильно (no degradation RX).

### Апдейт 2026-02-02 (по факту)
- Закрыт сценарий раннего playback до готовности I2S TX:
  - добавлен ready flag после enable TX+RX,
  - в `audio_player` возвращён best-effort enable TX перед play.
- Tail/flush механизм (тишина после play) не трогали, регрессии “вечного писка” нет.

## 5) Следующий шаг (если продолжать голос)
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

## FIRE (2026-01-30) — производительность и решение по FPS

Статус:
- FIRE исправлен (компиляция/переменные tip-profile) и стабильно работает на лампе.
- Оптимизация сделана без изменения визуальной “скорости” огня: визуал оставлен как эталон.

Факт по производительности (лог ANIM_PERF):
- show стабильный ≈ 22.5 ms на кадр (физический нижний предел WS2812 768 LED).
- render для FIRE в среднем ≈ 21–22 ms, пики ≈ 37–38 ms после фиксов.
- total в среднем ≈ 43–45 ms при целевом FPS=22 (бюджет 45 ms), поэтому редкие miss возможны при системных всплесках (аудио/логи/радио).

Решение:
- FPS для матрицы фиксируем на 22 как production default.
- 25 FPS без архитектурных изменений упирается в show-time: при 25 FPS бюджет 40 ms, а один show уже ~22.5 ms, что оставляет ~17.5 ms на render (недостижимо для текущего FIRE без ухудшения качества/алгоритма).
