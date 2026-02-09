
## 0) Термины и уже существующие сущности (как есть сейчас)

### Voice Events (аудио словарь)

События уже заведены и стабильно мапятся на файлы `/spiffs/v/.../*.wav` (короткие имена, ADPCM WAV)【】【】. Важные для MultiNet:

* Session/Wake:
  `VOICE_EVT_WAKE_D:contentReference[oaicite:3]{index=3}ES:contentReference[oaicite:4]{index=4}_EVT_NO_CMD_TIMEOUT`, `VOICE_EVT_BUSY_ALREADY_LISTENING`【】
  и их реальные пути/варианты в `voice_events.c`【】.
* CommandEVT_CMD_OK`, `VOICE_EVT_CMD_FAIL`, `VOICE_EVT_CMD_UNS】.
* Server/OTA:
  `VOICE_EVT_NEED_THINKING_SERVER`, `VOICE_EVT_SERVER_*`, `VOICE_EVT】.

### Текущий Voice FSM (сейчас)

`voice_fsm` пока делает:

* на wake: включает индикт голосом `WAKE_DETECTED` и держит **wake session timeout 8000 ms**; если команд нет, говорит `NO_CMD_TIMEOUT`【】【】.
* после playback: **post-guard 300 ms**, чтобы не ловить э: `IDLE`, `SPEAKING`, `POST_GUARD`【】.

### Wсть интерфейсы init/detect/task_start/task_stop, модель odel"`【】.

---

## 1) Цель (что получаем после внедрения MultiNet)

1. **Always-on WakeNet** -word:

   * запускаем “командную сессию” MultiNet (EN) на ограниченное время,
   * принимаем одну или несколько команд,
   * корректно выходим обратно в idle/WakeNet:

     * либо по **cancel-слову** (твой “прервать мультинет и вернуть idle”),
     * либо по **sleep-команде** (отдельно, уводит систему в soft sleep/off),
     * либо по **таймеру** (no command timeout).
2. Каждая распознанная команда приводит к **физическому действию** + **аудио-ответу** через существующие Voice Events (без “генеративных фраз”).
3. Не ломаем инварианты:

   * **no-interrupt policy**: текущий плеер не прерываем (если занят, возвращаем FAIL/BUSY)【】.
   * **post-guard** после речи обязателен【】.
   * wake session, используем как базовый лимит сессии【

## 2) Ответ на твой вопрос “почему нельзя sleep-word как обычную команду”

Логв: “disregard” и “next animation” одинаковы как *команды*. Разница обычно **в политике выхода**:

* “обычная команда” продолжает сессию,
* “cancel/disregard” **завершает** сессию и возвращает систему в idle/WakeNet,
* “sleep” завершает сессию и переводит систему в *другой power state*.

Так что мы делаем ровно то, что ты предлагаешь, но **без трюков типа brighten+dim** (опасно, может влиять на UX/состояние и гадко отлаживается). Мы вводим **явный командный ярлык** `ASR_CMD_CANCEL_SESSION`, который не делает “физики”, а делает “политику выхода” + голос `VOICE_EVT_SESSION_CANCELLED`【】.

---

## 3) Архитектура интеграции (модули и границы ответственности)

### 3.1 Новый`asr_multinet.{c,h}`** (название условно, но смысл такой):

Ответственность:

* init/deinit MultiNet (загрузка модели EN, создание handle),
* запуск/останов “командной сессии”,
* выдача распознанных команд в виде **фиксированного enum** (не строк).

Минимальный API (ТЗ):

* `esp_err_t asr_multinet_init(void);`
* `void asr_multinet_deinit(void);`
* `esp_err_t asr_multinet_start_session(void);`
* `void asr_multinet_stop_session(void);`
* `bool asr_multinet_is_session_active(void);`
* callback/event: `asr_cmd_t` в очередь (пример: `ASR_CMD_NEXT_EFFECT`, `ASR_CMD_CANCEL_SESSION`, ...).

### 3.2 Кто рулит состояниями

Чтобы не разнести проект в разные стороны, делаем так:

* `voice_fsm` становится **координатором “сессии слушания”**:

  * wake → стартует сессию,
  * timeout → закрывает,
  * playback/post_guard → временно “глушит” распознавание (см. ниже).

* `wake_wakenet_task` остаётся источником событий wake (как сейчас)【】.

* `asr_multinet` читает аудио через тот же “моно 16 kHz s16” поток, что и WakeNet выдаёт команду.

---

## 4) Состояния системы (фактическая машина состояний)

Мы расширяем текущую FSM логически до:

1. **IDLE**

   * WakeNet task активен
   * MultiNet session не активен
   * Genie overlay выключен (если не активна wake-сессия)【】

2. **LISTENING_SESSION** (после wake)

   * WakeNet либо паузим, либо допускаем, ноше паузить, чтобы не ловить повторные wake на фоне речи/шума)
   * MultiNet активен
   * Таймер сессии: используем существующий `VOICE_WAKE_SESSION_TIMEOUT_MS = 8000` как базовый【】
   * На входе: голос `VOICE_EVT_WAKE_DETECTED` уже есть【】

3)роигрывается voice event (или другая речь)

* MultiNet д(иначе он поймает голос джина как команду)
* После SPEAKING: `POST_GUARD` 300 ms уже реализован【】

4. **POST_GUARD**

   * Всё распознавание выключено на 300 ms (как сейчас)【 Затем возврат либо в LISTENING_SESSION (если сессия ещё активна), либо в IDLE.входа/выхода из MultiNet

### 5.1 Wake → старт MultiNet

Когда приходит wake:

* если сессии нет:

  1. `genie_overlay_set_enabled(true)` (как сейчас)【】
  2. `voice_event_post(WAKE_DETECTED)` (как сейчас)【】
  3. посл `asr_multinet_start_session()` и запускаем таймер 8s.ивна:

  * говорить `VOICE_EVT_BUSY_ALREADY_LISTENING` и не перезапускать сессию【】.

### 5.2 Cancel-session (то, что ты уточнил)

**Отдельная ASR команда**, которая:

* нршает MultiNet session,
* возвращает систему в IDLE (WakeNet снова активен),
* голосит `VOICE_EVT_SESSION_CANCELLED`【】【】.

### 5.3 Sleep / Soft-sleep

Это другая команда (у тебя уже еерает текущий flow софт-выключения/сна,

* голосит `VOICE_EVT_SOFT_OFF_BYE` или `DEEP_SLEEP_BYE` по месту【】.

### 5.4 Таймаут (no command)

Если за `VOICE_WAKE_SESSION_TIMEOUT_MS` не пришло ни о

* выключаем overlay и говорим `VOICE_EVT_NO_CMD_TIMEOUT` (уже есть)【】,
* останавливаем MultiNet session,
* возвращаемся в IDLE.

---

## 6) Связка “ASR комтвие → Voice Event”

Твой вывод верный: сначала фиксируем “ярлыки” команд, а MultiNet просто выдаёт эти ярлыки.

### 6.1 Набор команд (минимально полезный, без перегруза)

(EN фразы будут на уровне “training/command set”, но в коде только enum)

**Playback/session**

* `CANCEL_SESSION` (disregard/cancel/never mind)
* `SLEEP` (если уже есть power flow)
* `OTA_ENTER` (опционально, если хочешь голосом стартовать OTA портал)

**Анимации**

* `NEXT_EFFECT`
* `PREV_EFFECT` (опционально)
* `PAUSE_TOGGLE` (или pause/resume отдельными)

**Яркость/скорость/громкость**

* `BRIGHTNESS_UP`, `BRIGHTNESS_DOWN`
* `SPEED_UP`, `SPEED_DOWN`
* `VOLUME_UP`, `VOLUME_DOWN`, `MUTE_TOGGLE` (если mute есть как концепт)

**Server**

* `ASK_SERVER` (триггерит `VOICE_EVT_NEED_THINKING_SERVER` и дальнейшую механику)【】【】

### 6.2 Ответы голосом (строго из существующих Voice Events→ 】

* Команда не удалась (например, ctrl_bus вернул ошибку) → `VOICE_EVT_CMD_FAIL`【манда распознана как “неподдерживаемая” → `VOICE_EVT_CMD_UNSUPPORTED`【session → `VOICE_EVT_SESSION_CANCELLED`【】
* Ask serverNEED_THINKING_SERVER`, дальше по результату `SERVER】

---

## 7) Реализация распознавания MultiNet (как это “логически происходит”)

Multi”, он делает классификацию по фиксированному набору команд в модели. То есть это не LLM и не “перевести любую фразу в действие”. Это именно:
**вход аудио → вероятность по классам → победивший класс → наш `asr_cmd_t`**.
Поэтому да: нам нужен **список ASR-команд** отдельно от **списка аудио-событий**. Аудио-события это “что сказать в ответ”, ASR-команды это “что пользователь может сказать джину”.

---

## 8) Интеграция в код (план правок на следующем шаге, без патчей пока)

Когда ты скажешь “погнали править”, я буду делать по твоим правилам: точные места, 1–2 строки контекста, максимум 5 простых операций за шаг, чекпойнт-коммиты.

### Шаги интеграции (высокоуровнево)

1. **Добавить модуль `asr_multinet`** (пустой скелет + init/deinit + start/stop session).
2. Подтянуть ESP-SR MultiNet EN модель и настроить загрузку (обычно через partition `model`, как у WakeNet)【】.
3. Встроить в `voice_fsm` состояние LISTENING_SESSION:

   * после wake reply + potiNet session,
   * при получении `asr_cmd_t` выполнять действие и отвечать `CMD_OK/FAIL/...`,
   * cancel-session завершает сессию и возвращает IDLE,
   * timeout завершает сессию и `NO_CMD_TIMEOUT` уже есть【】.
4. Политика “не слушать себя”:

   * на время `SPEAKING` и `POST_GUARD` отключать Mul тоже). Post-guard уже 300 ms【】.
5. Привязать команды к реальным API управления (вероятно `ctrl_bus`), и на каждом де*EVT_CMD**`【】.
6. Отладочный лог (обязательно):

   * вход/выход session,
   * какая команда распознда (cancel / sleep / timeout / error),
   * счётчики в diag (по аналогии с `voice_fsm_diag_t`)【】.
