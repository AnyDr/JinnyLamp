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
- state_seq (uint32, монотонный)

Примечание (важно):
- На текущем этапе "power" НЕ является полем ctrl_bus и не передаётся в ACK.
- Команда ESPNOW POWER (0/1) сейчас маппится на paused+brightness=0/restore (v1 semantics).
- Полноценный SOFT OFF (stop join + DATA=LOW + MOSFET OFF) будет отдельной логикой питания,
  не смешиваемой с paused/brightness.


Источники команд:
- ESPNOW RX (пульт)
- TTP223 (локально)
- голосовые события (wake/commands/fallback)
- (опционально) серверные команды через bridge

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
FS монтируется на data partition `storage` в `/spiffs`.

Требования:
- диагностический list `/spiffs` + печать usage (used/total/free)
- код должен быть написан так, чтобы переход на LittleFS был “заменой backend”:
  один модуль `storage_*` с одинаковыми API, и минимальные изменения вне него.

Структура под голос:
- `/spiffs/voice/phrases/...` (voice pack)
- `/spiffs/voice/phrases/manifest.txt`

---

## 7) DOA (Direction of Arrival) — сервис данных всегда включён
DOA реализован как **always-on сервис**, доступный другим компонентам независимо от debug/UI.

### 7.1 Источник DOA
- XVF команда: `AEC_AZIMUTH_VALUES`
- payload: 4×float32 LE, radians
- используем: выбранный beam (проектный выбор зафиксирован)
- обновление: 10 Hz
- при невалидных данных: age + “grace period” до первого валидного чтения

### 7.2 Debug-обвязка DOA
DOA debug включается compile-time define в `fx_effects_doa_debug.c` и влияет только на:
- публикацию DOA FX в список эффектов (для пульта)
- DOA debug лог в монитор

В релизе:
- DOA данные продолжают жить и доступны всем,
- но DOA FX и debug лог отключены.

---

## 8) Audio: общая картина
- I2C: управление XVF + телеметрия
- I2S RX: 16 kHz, 2ch, 24-in-32 (для wake/ASR приводим к mono s16)
- I2S TX: воспроизведение аудио-ответа

Текущие наблюдения по smoke test:
- TX playback работает (звук слышен)
- на стыке TX/RX есть проблемы “duplex-safe”:
  - `i2s_channel_enable(): already enabled`
  - `audio_i2s_read err=ESP_ERR_TIMEOUT ...` после проигрывания

Причина: арбитраж I2S RX/TX ещё не доведён до строго корректного режима.

---

## 9) Audio IN: audio_stream service (RX single-owner)
Чтение I2S RX централизовано в `audio_stream`.
Инвариант: `audio_i2s_read()` вызывается только в `audio_stream.c`.

Пайплайн:
- `audio_stream` читает stereo int32 из `audio_i2s_read()`,
  конвертирует в **mono s16 @ 16 kHz** и пишет в ringbuffer.
- Потребители читают только через API `audio_stream_*`.

Потребители:
- `asr_debug` — только ringbuffer (не I2S)
- `wake_engine` (WakeNet)
- `cmd_engine` (MultiNet)

Режимы `audio_stream` (для безопасного voice-cycle):
- NORMAL: читаем RX → пишем ringbuffer
- DROP: читаем RX (опционально) → не пишем (или сразу discard)
- PAUSED: producer task приостанавливается (по арбитражу), ringbuffer flush

Важно: PAUSED/DROP используются voice_fsm на время SPEAK, чтобы минимизировать конфликты и self-feedback,
без физического дергания i2s enable/disable.

---

## 10) Audio OUT: audio_player (TX single-owner)
`audio_player` — отдельный модуль/таск, единственный владелец записи в I2S TX.

Воспроизведение:
- storage file → (decode если нужно) → PCM s16 mono 16k → pack to stereo word32 → `audio_i2s_write`

Требование:
- `audio_player` не должен напрямую решать “enable/disable channel” без арбитра.
- любые tx_set_enabled()/enable должны быть идемпотентны и контролироваться одним местом.

---

## 11) I2S арбитраж (P0)
Цель: RX и TX должны мирно жить вместе (duplex-safe), без таймаутов и “already enabled”.

Принятая политика (P0):
- I2S поднимается один раз в режиме duplex (если железо поддерживает) и далее не “дергается” на каждый playback.
- На время SPEAK:
  - voice_fsm переводит `audio_stream` в PAUSED/DROP + flush,
  - wake/multinet выключены,
  - затем `audio_player` играет фразу,
  - после завершения: post-guard, flush, `audio_stream` NORMAL, wake ON.
- Повторный enable/disable I2S каналов запрещён; состояние каналов хранит и обслуживает один арбитр.

---

## 12) Voice architecture (WakeNet + MultiNet + server fallback)
### 12.1 Цель
Сделать голосовой цикл без конфликтов владения ресурсами и без самовозбуждения:
- слушаем (WakeNet)
- после wake распознаём команду (MultiNet)
- если локально не распознали: “мне надо подумать” → сервер → команда/таймаут
- выполняем команду от сервера → подтверждение → “я слушаю”
- во время SPEAK лампа не слушает

### 12.2 Машина состояний voice_fsm
Состояния:
- IDLE: WakeNet активен
- LISTENING: MultiNet активен (окно команды)
- PROCESS_LOCAL: маршрутизация результата
- SPEAK_THINK: “мне надо подумать”
- WAIT_SERVER: ожидание `SERVER_CMD` или таймаута
- EXEC_SERVER_CMD: выполнение команды от сервера
- SPEAK_CONFIRM: подтверждение выполнения / таймаута
- COOLDOWN: post-guard
- IDLE

События:
- EVT_WAKE_DETECTED
- EVT_LOCAL_CMD_OK(cmd_id, params)
- EVT_LOCAL_CMD_FAIL
- EVT_SERVER_CMD(req_id, cmd_id, params)
- EVT_SERVER_TIMEOUT(req_id)
- EVT_SPEAK_DONE(phrase_id)
- EVT_TIMEOUT (listen window)
- EVT_CANCEL

Корреляция с сервером:
- каждый server fallback запрос содержит `req_id` (uint32), сервер отвечает с тем же `req_id`.

---

## 13) Voice phrases: события → ключи → варианты → random без повторов
Ответ лампы реализуется **проигрыванием заранее созданных аудио файлов** из storage.

### 13.1 Ключи и события
- В коде событие задаётся enum: `voice_phrase_id_t`.
- Каждое событие мапится на строковый ключ `key` (каноническое имя).
  Пример: `VPH_THINKING → "thinking"`, `VPH_CMD_OK → "cmd_ok"`.

### 13.2 Формат хранения (целевой): ADPCM в storage
Тестовый smoke path может использовать PCM, но целевой формат хранения voice pack:
- **ADPCM** в `/spiffs/voice/phrases/` для экономии места.
- Рекомендуемый вариант: **IMA ADPCM 4-bit** (фиксируется проектом; если изменится — обновить этот раздел).

Пайплайн:
ADPCM (storage) → decode → PCM s16 mono 16k → I2S TX.

### 13.3 Именование файлов
`<key>__<variant>.adpcm`, где `<variant>` = `01..03`.

Пример:
- `/spiffs/voice/phrases/thinking__01.adpcm`
- `/spiffs/voice/phrases/thinking__02.adpcm`
- `/spiffs/voice/phrases/cmd_ok__01.adpcm`

### 13.4 Manifest для обновления без перепрошивки
`/spiffs/voice/phrases/manifest.txt`:
- `key=N` (количество вариантов)
- опционально `voice_pack_version=...`

Если манифест отсутствует/битый — используется встроенный дефолт (fail-safe).

### 13.5 Random без повторов (shuffle bag), reset по reboot
Для каждого key с N вариантами:
- выбираем случайно из ещё не игранных в текущем круге
- исключаем повтор, пока все варианты не проиграны по 1 разу
- после исчерпания круга сбрасываем
- состояние хранится в RAM и сбрасывается при reboot

### 13.6 Обновление voice pack (не OTA)
OTA обновляет только app partitions.
Voice pack (ADPCM + manifest) обновляется по проводу в `storage` (SPIFFS).


## Power State Model

Jinny Lamp implements a strict three-level power model to avoid remote desynchronization
and undefined wake behaviour.
### Current implementation status (as of 2026-01-29)

- Remote command `J_ESN_CMD_POWER` is currently implemented as:
  OFF -> paused=1 + brightness=0 (remember last non-zero brightness)
  ON  -> paused=0 + brightness restore
  This does NOT stop matrix_anim and does NOT switch MOSFET power.

- Target behaviour (to be implemented):
  Remote POWER OFF -> enter SOFT OFF:
    stop(join) -> DATA=LOW -> MOSFET OFF
    DOA stays alive
    ESPNOW stays alive
  Local long-press (TTP223) -> DEEP SLEEP only (local wake).


### States

#### ON
- LED matrices powered (MOSFET ON)
- FX engine and animations active
- Audio RX (WakeNet / ASR) active
- Audio TX (voice playback) active
- ESPNOW / radio active

#### SOFT OFF (default OFF state)
Remote-friendly standby mode.

- LED matrices powered OFF (MOSFET OFF)
- FX engine stopped
- Audio RX remains active (voice wake supported)
- Audio TX active
- ESPNOW / radio active
- Lamp can be powered ON via:
  - Remote (ESPNOW)
  - Voice command
  - Local button

This is the default OFF state for:
- Remote power commands
- Voice power commands

#### DEEP SLEEP
Local-only hard power down.

- Entered only via physical button
- Wake source: TTP223 only
- Radio and audio fully powered down
- Remote wake is impossible by design

#### Power Loss
- Full reset
- Boot greeting played once after startup
