                                            Definition of Done (DoD) + План работ

1) Definition of Done: когда проект “Jinny Lamp” считается завершённым

## 1.1 Общие инварианты (не обсуждается)
WS2812 power sequencing соблюдается всегда:
DATA=LOW → MOSFET ON → delay → send и stop anim → DATA=LOW → MOSFET OFF.

Единственный show owner: вывод на матрицу делается только из matrix_anim/низкоуровневого драйвера; overlays рисуют только в canvas.

Audio RX single-owner: `audio_i2s_read()` вызывается только из `audio_stream.c`.

Voice anti-feedback: во время SPEAK лампа **не слушает** (wake/ASR отключены), чтобы исключить самовозбуждение.

Storage voice policy: голосовые фразы (ADPCM) лежат в data-FS (`storage`→`/spiffs`) и обновляются **не через OTA**, а по проводу. OTA обновляет только код.

I2S state ownership: включение/выключение RX/TX не должно происходить “из разных мест”; один арбитр I2S режима, операции идемпотентны.

После cold boot лампа не светит “мусором”, не клинит в тасках, не ломает управление.

---

## 1.2 Управление (локально + пульт)
Сенсорная кнопка TTP223: short/double/triple/long работают стабильно; deep sleep/wake корректны.

Пульт (ESPNOW): power/pause/effect/brightness/speed работают, состояние подтверждается ACK и корректно отображается.

---

## 1.3 DOA (Direction of Arrival)
DOA стабильно читается с XVF3800 через I2C (источник: `AEC_AZIMUTH_VALUES` / выбранный beam).
DOA обновляется в фоне (probe/task), угол доступен другим компонентам всегда.

Debug-обвязка DOA (FX + лог) включается compile-time define в `fx_effects_doa_debug.c`.
В релизе debug-обвязка выключена, но DOA данные остаются доступны.

---

## 1.4 Flash layout (под голос + модели)
Разметка flash фиксируется в варианте:
- `ota_0` = 2112 KB
- `ota_1` = 2112 KB
- `model` = 1152 KB
- `storage` = 2688 KB

---

## 1.5 Storage filesystem
FS поднимается на partition `storage` и монтируется в `/spiffs` (SPIFFS).
Код писать backend-agnostic, чтобы миграция на LittleFS была заменой backend одного модуля `storage_*`.

---

## 1.6 Голосовой цикл (основная “фишка” проекта)

Цепочка должна работать end-to-end:

XVF3800 принимает звук и вычисляет DOA azimuth 0…360°.

XVF передаёт на ESP:
- поток аудио (I2S RX, 16 kHz, 2ch, 24-in-32)
- и DOA по I2C control-командам.

ESP распознаёт wake word (WakeNet).

С момента wake:
- состояние = LISTENING
- команда распознаётся локально (MultiNet, фиксированный набор команд v1), действие выполняется.

Если команда локально **не распознана**:
1) лампа немедленно отвечает фразой “мне надо подумать”,
2) отправляет данные на сервер через bridge (без включения STA на лампе) и ждёт ответ до `SERVER_WAIT_MAX_MS`,
3) получает ответ от сервера в виде распознанной команды или таймаут,
4) выполняет команду (если пришла) и проигрывает подтверждение выполнения,
   либо при таймауте проигрывает короткий ответ “не удалось/нет ответа”,
5) возвращается в режим “я слушаю” (WakeNet ON, post-guard + flush).

Ответ лампы выполняется проигрыванием заранее созданных файлов из storage:
- целевой формат хранения: **ADPCM** (рекомендованный вариант: IMA ADPCM 4-bit)
- пайплайн: ADPCM → decode → PCM s16 mono 16k → I2S TX
- на событие 1..3 варианта фразы; выбор случайный **без повторов**, пока все варианты не проиграны по 1 разу (reset по reboot).

Во время воспроизведения:
- лампа **не слушает** (wake/ASR выключены),
- `audio_stream` переводится в PAUSED/DROP + flush,
- post-guard после SPEAK,
- возврат в IDLE (“я слушаю”).

---

## 1.7 OTA (обязательная часть завершения)
Команда OTA_START приводит к:
- гарантированной остановке анимации (join),
- DATA=LOW, MOSFET OFF,
- запуску SoftAP + HTTP OTA портала,
- корректному завершению по успеху/ошибке/таймауту и возвращению в NORMAL.

---

## 1.8 Интеграция с домашним сервером (через Ethernet-мост)
В нормальном режиме лампа остаётся ESPNOW-only.

Ethernet-Bridge (в одной коробке с RPi5):
- принимает состояния/телеметрию по ESPNOW,
- передаёт в HA/сервер по Ethernet,
- команды от сервера отправляет peer’ам по ESPNOW.

---

2) План работ с чекпойнтами (Milestones)

M0. Стабильность платформы (блокер для всего)

### M0.1 Stop=Join для matrix_anim
Реализовать stop как blocking join. После stop не остаётся task, который трогает show/буфер.

### M0.2 Safe LED shutdown
helper: stop(join) → DATA=LOW → MOSFET OFF. Использовать для power-off, deep sleep, OTA.

### M0.3 OTA lifecycle (happy + timeout)
OTA_START → SoftAP/portal → upload → set boot → reboot → normal; timeout 300s.

✅ Приёмка M0: циклы power/OTA без “матрица не включилась”.

---

M1. DOA: XVF → ESP (DONE)
✅ Приёмка M1: угол адекватен, debug-обвязка отделена compile-time.

---

M2. Storage + Audio playback smoke test (DONE частично)
✅ Play работает (звук есть), FS работает (read/write/list/usage).
⚠ Есть проблемы на стыке TX/RX (duplex-safe): таймауты RX после TX и “already enabled”.

---

M3. P0: I2S арбитраж (обязательно)
Цель: RX и TX живут вместе без таймаутов и “already enabled”.

Требования:
- один владелец состояния I2S RX/TX (арбитр)
- запрет повторного enable/disable на уже включённом канале
- политика voice-cycle:
  - на SPEAK: pause/drop/flush `audio_stream`, disable wake/multinet
  - playback через `audio_player`
  - post-guard + flush + `audio_stream` normal + wake ON
- тест: многократные play подряд и затем устойчивый RX (без ESP_ERR_TIMEOUT лавины)

✅ Приёмка M3:
- 50 play подряд без деградации RX
- после play `audio_stream` продолжает давать валидные данные

---

M4. Wake word (WakeNet)
- модель хранится в `model` partition
- интеграция после M3 (иначе хаос по таймингам)

---

M5. MultiNet commands + server fallback
- набор команд v1
- server fallback с req_id + timeout + confirm

---

M6. Voice phrases (ADPCM pack)
- manifest + 1..3 варианта на событие
- shuffle-bag random без повторов
- обновление voice pack по проводу (без OTA)

---

M7. Genie overlays (LISTEN/SPEAK) + синхронизация по FSM
- LISTEN ориентирован по DOA
- SPEAK “рот” живёт от аудио TX уровня или паттерна

---

M8. Ethernet bridge (RPi5) + HA integration
- ESPNOW hub ≤ 15 peers
- Ethernet uplink (MQTT/HTTP)

