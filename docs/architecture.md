# Архитектура Jinny Lamp

Jinny Lamp — прошивка лампы (ESP32-S3 host) на плате ReSpeaker XVF3800 (voice DSP) + WS2812 матрицы.
Лампа — **источник истины** состояния (effect/brightness/speed/paused/power/state_seq). Пульт — клиент: шлёт команды и обновляет UI по ACK.

Каналы управления:
- локально: сенсорная кнопка TTP223 (polling)
- удалённо: ESP-NOW (основной канал)
- OTA: SoftAP + HTTP upload (активируется командой OTA_START по ESP-NOW)

## Инварианты (не ломать)
1) **Single show owner:** только `matrix_anim` вызывает `matrix_ws2812_show()`.
2) **WS2812 power sequencing:**
   - ON: DATA=LOW → MOSFET ON → delay → show
   - OFF: stop(join) → DATA=LOW → MOSFET OFF
3) **OTA safety / power-off / deep sleep:** перед отключениями всегда stop(join) и “LED safe”.

---

## 1) LED pipeline (render → show)
- `matrix_anim` (task ~10 FPS):
  - `fx_engine_render(t_ms)` рисует в canvas
  - `matrix_ws2812_show()` пушит на WS2812 (единственный show)

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
- power (bool, если используется)
- state_seq (uint32, монотонный)

Источники команд:
- ESPNOW RX (пульт)
- TTP223 (локально)
- (в будущем) голосовые события/режимы

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
- timeout (напр. 300s) → выход (reboot/cleanup по политике)

Rollback:
- rollback включён, anti-rollback выключен (для dev)

---

## 6) Audio / XVF3800
- I2C: управление XVF + чтение телеметрии (в т.ч. GPO MOSFET)
- I2S RX: 16 kHz, 2ch, 24-in-32 (под wake/ASR приводим к mono s16)
- I2S TX: (план) воспроизведение аудио-ответа

`asr_debug` — диагностическая задача (шум/уровни), по необходимости ограничивать лог-уровень.

---

## 7) DOA (Direction of Arrival)
DOA реализован как **always-on сервис**, доступный другим компонентам независимо от debug/UI.

### 7.1 Источник DOA (текущее)
- XVF команда: `AEC_AZIMUTH_VALUES`
- payload: 4×float32 LE, radians
- используем: индекс [3] **auto-selected beam**
- обновление: 10 Hz
- при невалидных данных: hold last + fade (на стороне потребителя)

### 7.2 Debug-визуализация (опционально)
- FX `DOA DEBUG` рисует 1 пиксель (X=угол, Y=уровень)
- Debug влияет только на:
  - наличие `DOA DEBUG` в списке эффектов (для пульта)
  - лог DOA в monitor
- Сервис DOA при этом остаётся активным всегда.

Параметры калибровки:
- `offset_deg`, `inv`, wrap 0..360
- deadband / сглаживание (если включено политикой)

---

## 8) Tasks / ownership (тезисно)
- `matrix_anim`: единственный владелец show
- `ota_portal`: единственный владелец OTA lifecycle
- `ctrl_bus`: единственный владелец состояния
- `j_espnow_link`: транспорт RX/TX команд + ACK
- `doa_probe`: always-on DOA snapshot

## 8) Audio IN (M2): audio_stream service (RX single-owner)

С 2026-01: чтение I2S RX централизовано в модуле `audio_stream`.
Инвариант: `audio_i2s_read()` вызывается только в `audio_stream.c` (единственный владелец RX).

Пайплайн:
- `audio_i2s` поднимает I2S RX/TX (16 kHz, 2ch, 24-in-32)
- `audio_stream` (producer task) читает stereo int32 из `audio_i2s_read()`,
  конвертирует в **mono s16 @ 16 kHz** и пишет в ringbuffer.
- Потребители читают только через:
  - `audio_stream_read_mono_s16(int16_t* dst, size_t samples, size_t* out, TickType_t to)`

Потребители:
- `asr_debug` больше не читает I2S напрямую (только ringbuffer `audio_stream`).
- Будущий `wake_engine` (WakeNet) будет подключён через тот же API.

Надёжность:
- Ошибки/timeout в `audio_stream`/`asr_debug` не приводят к `esp_restart()` (debug деградирует мягко).
- Ringbuffer: 8×512 samples (~256 ms) по умолчанию; при переполнении фреймы дропаются (счётчик drop_frames).
