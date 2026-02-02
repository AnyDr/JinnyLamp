
* **I2S TX реально играет** (через `audio_player` → `audio_i2s_write()`), динамик через усилитель платы XVF.
* **Громкость 0..100**, и она должна **переживать перезагрузку** (то есть NVS persist).
* События/состояния должны быть **масштабируемыми** (не “enum на весь проект”).

## Цель этапа

1. Ввести **масштабируемый lifecycle/event слой** (state-machine) поверх текущего поведения soft-on/soft-off и voice events.
2. Добавить **Audio Volume** как параметр устройства:

* управляется с пульта по ESPNOW,
* применяется мгновенно,
* сохраняется в NVS (переживает reboot),
* архитектура протокола заложена сразу.

---

## Инварианты

* FPS матрицы **22**.
* FIRE/FX не трогаем (кроме будущих “реакций на состояние” через контроллер).
* `audio_player` остаётся single-flight. 
* Не делаем больших переносов/переименований.

---

## Архитектурное решение (масштабируемость)

### 1) Events: домены + payload

Вместо “бесконечного enum”:

* `event_id` = 16-bit: `domain (high byte) | code (low byte)`
* `payload` = union (`u32/i32/ptr/struct`)
* очередь событий (FreeRTOS queue) как единая точка входа

Так ты спокойно добавляешь новые источники (ESPNOW/Audio/Timers/Buttons) без разрастания адской таблицы.

### 2) States: небольшая FSM + расширяемые “активности”

FSM держим узкой: OFF/BOOTING/ACTIVE/SHUTTING_DOWN (+ при необходимости SPEAKING как activity-флаг).
Детали роста (OTA mode, speaking overlay, future wake/ASR) лучше описывать “activity flags” внутри ACTIVE, чтобы не плодить переходы.

---

## Протокол ESPNOW: Volume (закладываем сразу)

### Команды (lamp side)

Расширяем `j_esn_cmd_t` (в `j_espnow_proto.h`) новыми командами: 

* `J_ESN_CMD_SET_VOLUME`  (value_u16: 0..100)
* `J_ESN_CMD_GET_VOLUME`  (value_u16 unused)

### Ответ/синхронизация

Тут два варианта; я предлагаю **самый безопасный по совместимости**:

**Вариант A (рекомендую): оставить ACK как есть, добавить отдельный HELLO-RSP для volume**

* `J_ESN_HELLO_AUDIO_VOL_RSP` (lamp → remote)

  * `vol_pct (u8)`
  * `status (u8)`
  * `state_seq (u32)` (чтобы UI понимал актуальность)

Плюсы:

* не ломаем существующий ACK layout вообще
* можно слать volume ответ и при SET, и по запросу, и при входе в OTA overlay

Минусы:

* чуть больше сообщений

**Вариант B (быстрее, но требует аккуратного апдейта пульта): расширить `j_esn_ack_t` полем volume в хвосте**

* добавляем `uint8_t volume_pct; uint8_t rsvX;`
* RX на пульте должен принимать payload >= старого размера и не падать

Если ты хочешь “как можно меньше типов сообщений” и готов синхронно обновить пульт, тогда B ок. Если хочешь страховку и меньше шансов словить “несовпадение структур”, берём A.

---

## Громкость на лампе: где применять

У тебя воспроизведение идёт через `audio_player.c`, который читает PCM s16 mono и конвертит в int32 stereo для I2S. 
Самое надёжное и минимально-инвазивное место:

* применить software gain **на s16 до упаковки** (`s16_to_i2s_word()`).

То есть:

* хранить `volume_pct` (0..100)
* переводить в коэффициент Q15 (0..32767)
* `s16 = (s16 * gain_q15) >> 15` с сатурацией

---

## Persist: NVS запись без убийства флеша

Тебе нужен ползунок: он может слать SET часто. Поэтому:

* применяем громкость **сразу** (UX)
* в NVS сохраняем **с debounce** (например 500–800 ms после последнего изменения)
* также делаем **форс-сейв** при shutdown/soft-off (если dirty)

---

# Детальный план работ (по шагам, без “прыжков”)

## M0. Git checkpoint + минимальная дока протокола

* Обновить docs: “Audio volume: команды, диапазон, persist policy”.
* Никакого поведения.

**Commit/tag**

* `git commit -m "docs: add audio volume control protocol (espnow)"`
* `git tag checkpoint/vol_proto_spec_v1`

---

## M1. Ввести settings-хранилище (NVS) под volume

Добавить маленький модуль (минимальный, без лишнего):

* `j_settings.h/.c`

  * `j_settings_init()`
  * `j_settings_get_u8(key, def)`
  * `j_settings_set_u8_debounced(key, val)` + таймер/служебный тик
  * `j_settings_flush()` (форс при shutdown)

Ключ:

* namespace `"jinny"`
* key `"audio_vol_pct"`

**Commit/tag**

* `git commit -m "settings: add NVS-backed audio volume (debounced persist)"`
* `git tag checkpoint/M1_settings_vol_nvs`

---

## M2. Audio player: API set/get volume + применение gain

Правки:

* `audio_player.h`: добавить

  * `void audio_player_set_volume_pct(uint8_t vol);`
  * `uint8_t audio_player_get_volume_pct(void);`
* `audio_player.c`:

  * хранить `s_volume_pct` (atomic/volatile достаточно)
  * применять gain при конвертации `s_in_s16[i]` → `s_out_i2s[...]`

Важно:

* clamp 0..100
* gain=0 → mute (всё в ноль)

**Commit/tag**

* `git commit -m "audio: add software volume (0..100) in audio_player"`
* `git tag checkpoint/M2_audio_player_volume_apply`

---

## M3. ESPNOW: принять команды SET/GET volume и ответить

Где именно зависит от твоего текущего обработчика RX (я его тут не вижу, только заголовок протокола и link_start).
Делаем:

* при `SET_VOLUME`:

  1. validate 0..100
  2. `audio_player_set_volume_pct(vol)`
  3. `j_settings_set_u8_debounced("audio_vol_pct", vol)`
  4. отправить ответ (HELLO_RSP или ACK-extended)
* при `GET_VOLUME`:

  * отправить текущее значение

**Commit/tag**

* `git commit -m "espnow: add audio volume SET/GET commands + response"`
* `git tag checkpoint/M3_esn_volume_cmds`

---

## M4. Boot sync: загрузить volume из NVS и применить

В init-потоке (main/app init):

* `j_settings_init()`
* `vol = get(key, default=70)`
* `audio_player_set_volume_pct(vol)`
* optionally: сразу отправить volume info пульту при HELLO/при входе в OTA режим (если у тебя есть “HELLO welcome”).

**Commit/tag**

* `git commit -m "boot: load persisted audio volume and apply to player"`
* `git tag checkpoint/M4_vol_boot_sync`

---

## M5. Lifecycle: заложить масштабируемые event/state + hook для flush

Когда начнём state-machine:

* on transition to `SHUTTING_DOWN/OFF`:

  * `j_settings_flush()` (если dirty)
* события ESPNOW volume идут как `EVT_ESN_CMD_SET_VOLUME` в общую очередь (масштабируемость)

**Commit/tag**

* `git commit -m "lifecycle: scalable events/state + settings flush on shutdown"`
* `git tag checkpoint/M5_lifecycle_vol_hooks`

---

# Риски и как их прибить

1. **Износ флеша** из-за частых SET от слайдера
   → debounce persist + форс flush на shutdown.

2. **Совместимость протокола** (ACK layout)
   → рекомендую вариант A: отдельный HELLO_RSP для volume.

3. **Перегруз CPU** из-за умножения на каждый sample
   → это дешёво на S3 (16 kHz, 256 samples chunk). Влияние будет мизерным по сравнению с WS2812 show-time.

## Проверка (2026-02-02): результаты
- Volume на лампе реализован полностью: ESPNOW SET/GET + HELLO rsp, persist NVS с debounce.
- Voice boot greeting реализован: 3 файла, random без повторов, persistent mask.
- Soak 50 plays: в установившемся режиме проходит стабильно.

## Выявленная проблема
- Early playback: если play запускается до готовности I2S/TX, возможен ESP_ERR_INVALID_STATE ("channel not enabled").
  Причина: player не гарантирует enable TX перед write, а lifecycle не gate'ит voice events по audio_ready.

## Решение (выбрать одно)
A) Добавить audio_ready флаг в lifecycle/FSM (предпочтительно архитектурно).
B) Вернуть best-effort audio_i2s_tx_set_enabled(true) в audio_player перед первым write (минимально-инвазивно).
