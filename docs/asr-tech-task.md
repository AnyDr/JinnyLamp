Ок. Ни «фигня», ни философия. Ниже прямое **ТЗ (requirements + пошаговая интеграция)** для MultiNet в Jinny Lamp **с того места, где srmodels.bin уже прошит в `model` partition**, и дальше до “работает end-to-end”. Буду опираться на то, что у тебя сейчас реально есть: `model` partition отдельный, `storage` (SPIFFS) отдельно, и текущая заготовка `asr_multinet.c` уже умеет грузить модель из `model` и крутить `detect()` на аудиочанках.

---

# ТЗ: Интеграция MultiNet (ESP-SR) в работу лампы

## 0) Инварианты и границы (что НЕ меняем)

1. **WakeNet always-on в IDLE**, MultiNet **включается только после wake-word** и живёт в пределах voice-сессии.
2. Во время **SPEAKING (playback)**: ASR **заглушен** (WakeNet и MultiNet не должны потреблять/мешать аудио).
3. **No-interrupt**: аудио-ответы не прерываются.
4. Модели MultiNet/WakeNet лежат **не в OTA слотах**, а в **отдельном `model` partition**, обновляются “по проводу” редко.
5. SPIFFS (`storage`) содержит **voice pack** и прочее, но **не модели SR**.

---

## 1) Память/флеш: что должно быть уже готово (у тебя уже готово)

### 1.1 Partition table

* `model` partition: type `data`, subtype `64`, размер достаточный под `srmodels.bin` (у тебя сейчас 2.5 MB).
* `storage` partition: `spiffs` под voice pack (у тебя сейчас ~2.4 MB).

Требование:

* `srmodels.bin` прошит в offset `model` partition.
* В рантайме MultiNet грузит модели по имени partition `"model"`.

---

## 2) Архитектура модулей (кто кому хозяин)

### 2.1 Владелец сессии

**`voice_fsm`** — единственный координатор:

* решает, когда стартовать MultiNet
* решает, когда стопать
* принимает результат распознавания и мапит в действия/voice events

### 2.2 asr_multinet

**`asr_multinet`** — “движок распознавания”:

* init: загрузить модель из `model` partition, создать MN handle
* start_session: включить цикл чтения аудио и detect
* stop_session: остановить цикл (без удаления модели)
* deinit: destroy handle, deinit model list, удалить таск/evgroup
* выдавать результат **через callback** в `voice_fsm`, без прямых вызовов ctrl_bus / voice_events

### 2.3 audio_stream

`audio_stream_read_mono_s16()` — источник PCM16 mono 16 kHz для ASR:

* MultiNet читает только когда сессия активна
* формат и частота должны совпадать с ожиданиями MN (16k, s16, mono)

---

## 3) API контракт (обязательный, чтобы не было линкер-ада)

Сейчас у тебя рассинхрон header vs implementation (видно по файлам).
Требование: привести к **одному** контракту.

### 3.1 asr_multinet.h (единый контракт)

Должно быть:

* `esp_err_t asr_multinet_init(asr_multinet_result_cb_t cb, void *user_ctx);`
* `void asr_multinet_deinit(void);`
* `esp_err_t asr_multinet_start_session(uint32_t timeout_ms);`
* `esp_err_t asr_multinet_stop_session(void);`
* `bool asr_multinet_is_active(void);`

Результат:

* `asr_multinet.c` реализует ровно эти имена/сигнатуры
* `voice_fsm.c` использует **только** эти функции, без “start() vs start_session()” зоопарка.

---

## 4) Модель команд: “ярлыки” и mapping

Тебе нужно “список команд” и “транспортировщик”. Делается в два слоя:

### 4.1 Слой A: MultiNet command table (ID ↔ phrases)

Источник истины: **конфиг/таблица в коде** (позже может быть Kconfig, но сейчас код).

Требования:

* фиксированный список `command_id` (int) и N фраз (строки).
* обязательная проверка `esp_mn_commands_update()` на ошибки; если есть ошибки, логировать и считать init failed.

Пример структуры (концепт):

* `command_id=1` → ["cancel", "stop listening", ...]
* `command_id=2` → ["sleep", ...]
* `command_id=10` → ["next", "next effect", ...]
  и т.д.

### 4.2 Слой B: Router (command_id → asr_cmd_t)

`voice_fsm` получает `command_id` и мапит в твой enum `asr_cmd_t` (как в хедере). 

Требование:

* mapping таблицей (switch/case допустим), без строковых сравнений в runtime
* unknown → `ASR_CMD_NONE`

---

## 5) FSM поведение: точные правила включения/выключения

### 5.1 Состояния (целевая схема)

* **IDLE**: WakeNet ON, MultiNet OFF
* **LISTENING_SESSION**: WakeNet OFF/paused, MultiNet ON, таймер session_timeout (например 8s)
* **SPEAKING**: WakeNet OFF, MultiNet OFF (или paused), playback
* **POST_GUARD**: 300 ms, затем обратно в LISTENING_SESSION или IDLE

### 5.2 Триггеры

1. WakeNet детект → `voice_fsm_on_wake()`:

   * если не SPEAKING:

     * перейти в LISTENING_SESSION
     * вызвать `asr_multinet_start_session(timeout_ms)`
     * запустить таймер сессии

2. При результате MultiNet:

   * `asr_multinet` вызывает callback в `voice_fsm` с `asr_cmd_result_t`
   * `voice_fsm`:

     * немедленно `asr_multinet_stop_session()`
     * применить действие (ctrl_bus / power / ota / ask server)
     * поставить voice_event реакцию (playback)
     * перейти в SPEAKING → POST_GUARD → (если сессия ещё актуальна) LISTENING_SESSION иначе IDLE

3. Timeout LISTENING_SESSION:

   * `asr_multinet_stop_session()`
   * voice_event “no command” (если нужно)
   * перейти в IDLE

4. CANCEL_SESSION:

   * не выполнять “физических действий”
   * проиграть VOICE_EVT_SESSION_CANCELLED
   * перейти в IDLE

---

## 6) Требования к реализации `asr_multinet` (внутренности)

На базе того, что у тебя уже сделано в `asr_multinet.c`: 

### 6.1 Init

* `esp_srmodel_init("model")`
* `esp_srmodel_filter(... ESP_MN_PREFIX, ESP_MN_ENGLISH)` (язык по требованию)
* `esp_mn_handle_from_name(mn_name)`
* `mn->create(mn_name, detect_length_ms)`
* `esp_mn_commands_clear/add/update`
* Создать task + eventgroup (но task может жить постоянно, работая по bit RUN)

**Ошибки init** должны быть фатальными: вернуть `ESP_FAIL` и не запускать session.

### 6.2 Session start/stop

* start_session:

  * поставить `RUN` бит
  * установить internal deadline = now + timeout_ms
* stop_session:

  * снять `RUN` бит

**Важно:** stop_session не уничтожает модели.

### 6.3 Task loop

* пока RUN:

  * читать `audio_stream_read_mono_s16(dst, chunk_samples, &samples_read, timeout_ticks)`
  * пропускать partial chunks
  * `mn->detect(handle, pcm)`
  * если `r>0`:

    * получить результаты (`mn->get_results(handle)`) и распарсить поля *в соответствии с твоей версией заголовков*
    * сформировать `asr_cmd_result_t`:

      * command_id (обязательно)
      * prob/phrase_id/label best-effort
    * вызвать callback
    * **не принимать новый результат, пока voice_fsm не перезапустит сессию** (проще всего: `stop_session()` внутри asr_multinet после callback, либо voice_fsm сразу стопает)

---

## 7) Проверки/DoD по MultiNet (что значит “сделано”)

### 7.1 Build-time

* чистая сборка без варнингов по сигнатурам
* нет “undefined reference”

### 7.2 Runtime лог

1. На boot:

   * `ASR_MN: MultiNet ready (model=...)`
2. После wake-word:

   * `voice_fsm: start session`
   * `ASR_MN: session start timeout=...`
3. На команду:

   * `ASR_MN: detected cmd_id=... prob=...`
   * `voice_fsm: stop session`
   * действие выполнено + voice_event сыгран
4. На timeout:

   * `voice_fsm: session timeout`
   * optional voice_event “no cmd”
   * возврат в IDLE

### 7.3 Негативные тесты

* Во время playback MultiNet не жрёт аудио (не детектит ничего).
* При отсутствии `model` partition или битом `srmodels.bin` init падает корректно и система остаётся в IDLE без ребута.

---

## 8) Что нужно от тебя “двумя файлами” (ты обещал скинуть)

Чтобы сделать “правильный транспортировщик” без угадываний, мне нужно:

1. файл/таблица “**список команд**” (какие intent’ы реально хочешь)
2. файл/таблица “**voice event реакций**” (какой VOICE_EVT на какую команду)

После этого мы:

* формируем `esp_mn_commands_add(command_id, phrase)` набор
* мапим `command_id -> asr_cmd_t`
* мапим `asr_cmd_t -> действие + voice_evt`

---

Если хочешь, следующим сообщением я могу сразу дать **скелет “Router” таблицы** (command_id→asr_cmd_t→(action, voice_evt)) в формате, который ты просто вставишь в `voice_fsm.c`, но без твоего списка команд это будет гадание, а ты просил “чётко, подробно, понятно” без фантазий.
