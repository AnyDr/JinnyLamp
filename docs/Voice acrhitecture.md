# Jinny Lamp — Voice Pack & Voice Events (PCM v1)

## Цель
Зафиксировать “словарь голосовых событий” и правила озвучки Jinny Lamp на этапе PCM, чтобы:
- список событий был масштабируемым и стабильным,
- аудиофайлы были человекочитаемо именованы и структурированы,
- выбор вариантов был детерминированным и безопасным (циклический shuffle-bag),
- было понятно, как добавлять новые события и файлы без сюрпризов.

На данном этапе используется **PCM**. Переход на **IMA ADPCM 4-bit (EN)** выполняется отдельной задачей после стабилизации событий/пайплайна.

---

## Инварианты (не ломать)
1) **Voice anti-feedback:** во время SPEAK лампа не слушает (wake/ASR выключены).
2) **Audio RX single-owner:** `audio_i2s_read()` вызывается только из `audio_stream.c`.
3) **Voice pack read-only:** файлы на SPIFFS используются только для чтения в рантайме.
4) **Single voice output path:** воспроизведение выполняется через `audio_player`.
5) **No-interrupt policy (v1):** voice events не прерывают текущее воспроизведение. Если плеер занят — применяется политика busy (см. ниже).

---

## Текущее хранилище и структура каталогов (SPIFFS)
Корень voice pack:
- `/spiffs/voice`

Смысловая структура (v1):
- `/spiffs/voice/lifecycle/` — питание/сон/жизненный цикл
- `/spiffs/voice/session/`   — wake-session слой
- `/spiffs/voice/cmd/`       — подтверждения выполнения команд
- `/spiffs/voice/server/`    — ветка “думаю/сервер/таймаут”
- `/spiffs/voice/ota/`       — OTA lifecycle
- `/spiffs/voice/error/`     — ошибки/диагностика

> Примечание: языковой каталог (`/en/`) будет добавлен на шаге перехода на ADPCM.
> Сейчас язык не фиксируем каталогом, чтобы не усложнять PCM этап.

---

## Формат аудиофайлов (PCM, текущий этап)

Единый стандарт для всех voice event файлов:

- Контейнер: raw PCM (без WAV заголовка)
- Sample rate: **16000 Hz**
- Channels: **mono (1)**
- Sample format: **signed 16-bit little-endian (s16 LE)**

Рекомендации:
- избегать клиппинга, целевой peak ≤ -1 dBFS
- короткие ответы предпочтительнее длинных
- избегать длинных хвостов тишины (но допускается 50–150 ms аккуратной “паузы”)

---

## Нейминг файлов (строгий стандарт)

Каждое событие имеет **до 3 вариантов** (`v1..v3`).

Шаблон:
- `evt_<snake_case_event>__v{1..3}.pcm`

Полный путь:
- `/spiffs/voice/<group>/evt_<event>__v{1..3}.pcm`

Примеры:
- `/spiffs/voice/lifecycle/evt_boot_hello__v1.pcm`
- `/spiffs/voice/lifecycle/evt_soft_on_hello__v2.pcm`
- `/spiffs/voice/session/evt_no_cmd_timeout__v3.pcm`
- `/spiffs/voice/cmd/evt_cmd_ok__v1.pcm`

---

## Shuffle-bag (3 варианта) — обязательная логика

Для каждого события используется shuffle-bag по вариантам v1..v3:

1) Пока не проиграны все варианты — выбираем случайный из ещё не проигранных.
2) Когда все 3 варианта проиграны — **мешок сбрасывается**, и цикл начинается заново.
3) Исчерпание вариантов **НЕ является ошибкой**. Никаких “варианты кончились” ситуаций быть не должно.
4) В пределах одной wake-session желательно избегать повторов варианта для одного события, но после сброса мешка варианты снова доступны.

Persistency:
- для части событий мешок хранится persistently (NVS) и переживает reboot;
- для остальных — RAM-only и сбрасывается при reboot.

---

## Политика busy (плеер занят)

Система моноголосая, прерываний нет.

Если `voice_event_post()` вызывается, когда плеер занят:
- по умолчанию функция возвращает ошибку (например `ESP_ERR_INVALID_STATE`) и логирует событие,
- опционально “мелкие” события могут быть **dropped** (будущим флагом `drop_if_busy`).

На v1 допустима строгая политика: busy → error (без drop), чтобы FSM могла принять решение.

---

## Словарь событий v1 (C-style имена) и назначение

### A) Lifecycle / Power (`/spiffs/voice/lifecycle/`)

#### `VOICE_EVT_BOOT_HELLO`
- Когда: cold boot (после питания/ресета).
- Зачем: отдельное приветствие “я появился”.
- Файлы:
  - `evt_boot_hello__v1.pcm` (I am Alive! Again! Fuck!)
  - `evt_boot_hello__v2.pcm` (Rebirth! Evil laugh)
  - `evt_boot_hello__v3.pcm` (Here is Jinny!!)

#### `VOICE_EVT_DEEP_WAKE_HELLO`
- Когда: выход из deep sleep (локально).
- Файлы:
  - `evt_deep_wake_hello__v1.pcm` (I have been spleeping mortal)
  - `evt_deep_wake_hello__v2.pcm` (Oh nooo, that`s you again)
  - `evt_deep_wake_hello__v3.pcm` (You will regret this, mortal!)

#### `VOICE_EVT_DEEP_SLEEP_BYE`
- Когда: вход в deep sleep (локально).
- Файлы:
  - `evt_deep_sleep_bye__v1.pcm` (Finally, the long-awaited rest!)
  - `evt_deep_sleep_bye__v2.pcm` (Do not disturb me again after, mortal)
  - `evt_deep_sleep_bye__v3.pcm` (Farewell, suckers! laugh)

#### `VOICE_EVT_SOFT_ON_HELLO`
- Когда: выход из SOFT OFF (включили по пульту/серверу).
- Важно: это событие **отдельно** от `VOICE_EVT_BOOT_HELLO`.
- Файлы:
  - `evt_soft_on_hello__v1.pcm` (For your information - I was taking a nap)
  - `evt_soft_on_hello__v2.pcm` (Are you serious now? Oh come on, human! )
  - `evt_soft_on_hello__v3.pcm` (I was in another world, it was beautifull)

#### `VOICE_EVT_SOFT_OFF_BYE`
- Когда: вход в SOFT OFF (выключили по пульту/серверу).
- Файлы:
  - `evt_soft_off_bye__v1.pcm` (Do not do anything stupid while I`m gone)
  - `evt_soft_off_bye__v2.pcm` (I can oversleep an eternity if I wish)
  - `evt_soft_off_bye__v3.pcm` (Vader`s gone, carry on)

Persistency (рекомендация v1):
- persistent shuffle: `VOICE_EVT_BOOT_HELLO`, `VOICE_EVT_DEEP_WAKE_HELLO`, `VOICE_EVT_DEEP_SLEEP_BYE`
- RAM-only: `VOICE_EVT_SOFT_ON_HELLO`, `VOICE_EVT_SOFT_OFF_BYE`

---

### B) Session / Wake (`/spiffs/voice/session/`)

#### `VOICE_EVT_WAKE_DETECTED`
- Когда: WakeNet детектировал wake-word.
- Файлы:
  - `evt_wake_detected__v1.pcm` (Yes, my master?)
  - `evt_wake_detected__v2.pcm` (Whazaaaaaap!)
  - `evt_wake_detected__v3.pcm` (Speak, mortal one)

#### `VOICE_EVT_SESSION_CANCELLED`
- Когда: пользователь голосом отменил активную wake-session (sleep-word/cancel).
- Файлы:
  - `evt_session_cancelled__v1.pcm` (As you wish)
  - `evt_session_cancelled__v2.pcm` (always hate that)
  - `evt_session_cancelled__v3.pcm` (yeah yeah, fuck me)

#### `VOICE_EVT_NO_CMD_TIMEOUT`
- Когда: wake был, но команда не поступила в окно (например 8 секунд).
- Файлы:
  - `evt_no_cmd_timeout__v1.pcm` (No one even dare to speak with me? Good, good.)
  - `evt_no_cmd_timeout__v2.pcm` (Time is over, buuuuuy! )
  - `evt_no_cmd_timeout__v3.pcm` (Silense, love it)

#### `VOICE_EVT_BUSY_ALREADY_LISTENING`
- Когда: wake повторно во время уже активной session.
- Файлы:
  - `evt_busy_already_listening__v1.pcm` (Nope, already listening)
  - `evt_busy_already_listening__v2.pcm` (I am Busy, call later)
  - `evt_busy_already_listening__v3.pcm` (Get in line!)

---

### C) Command outcomes (`/spiffs/voice/cmd/`)

#### `VOICE_EVT_CMD_OK`
- Когда: команда выполнена успешно.
- Файлы:
  - `evt_cmd_ok__v1.pcm` (Your wish is my command my lord, done)
  - `evt_cmd_ok__v2.pcm` (If you wish so mortal)
  - `evt_cmd_ok__v3.pcm` (Done, so only one wish left)

#### `VOICE_EVT_CMD_FAIL`
- Когда: команда распознана, но не выполнена (ошибка/недоступно).
- Файлы:
  - `evt_cmd_fail__v1.pcm` (It is beyond of my powers)
  - `evt_cmd_fail__v2.pcm` (Something went terribly wrong)
  - `evt_cmd_fail__v3.pcm` (forgive me my master, I can`t)

#### `VOICE_EVT_CMD_UNSUPPORTED`
- Когда: команда распознана, но не поддерживается в этой версии.
- Файлы:
  - `evt_cmd_unsupported__v1.pcm` (we need more gold!)
  - `evt_cmd_unsupported__v2.pcm` (Subscription check - failed.)
  - `evt_cmd_unsupported__v3.pcm` (You haven`t paid for that)

> Примечание: конкретные “brightness up/down”, “speed up/down”, “anim next/prev” подтверждаются этим же `CMD_OK` на v1.

---

### D) Server / LLM flow (`/spiffs/voice/server/`)

#### `VOICE_EVT_NEED_THINKING_SERVER`
- Когда: локально не распознано, сервер доступен, отправляем запрос.
- Файлы:
  - `evt_need_thinking_server__v1.pcm` (I shall ask my superiors about that.)
  - `evt_need_thinking_server__v2.pcm` (let me drink about that)
  - `evt_need_thinking_server__v3.pcm` (I will ask the elder`s spririt)

#### `VOICE_EVT_SERVER_UNAVAILABLE`
- Когда: сервер/линк недоступен.
- Файлы:
  - `evt_server_unavailable__v1.pcm` (I am feeling disturbance in power, cannot ask)
  - `evt_server_unavailable__v2.pcm` (Spiritual Google is offline at the moment. I am sorry.)
  - `evt_server_unavailable__v3.pcm` (I am unable to foolow for that path for now)

#### `VOICE_EVT_SERVER_TIMEOUT`
- Когда: запрос отправлен, но ответ не пришёл за `SERVER_WAIT_MAX_MS`.
- Файлы:
  - `evt_server_timeout__v1.pcm` (The Consil have been ignored my request, my Lord)
  - `evt_server_timeout__v2.pcm` (Still no response from the Elders.)
  - `evt_server_timeout__v3.pcm` (They are still silent I am afraid)

#### `VOICE_EVT_SERVER_ERROR`
- Когда: сервер ответил ошибкой/невалидно.
- Файлы:
  - `evt_server_error__v1.pcm` (Server response are errorous)

---

### E) OTA lifecycle (`/spiffs/voice/ota/`)

#### `VOICE_EVT_OTA_ENTER`
- Когда: приняли OTA_START, переходим в OTA режим.
- Файлы:
  - `evt_ota_enter__v1.pcm` (OTA started)


#### `VOICE_EVT_OTA_OK`
- Когда: OTA upload/проверки успешны, сейчас будет reboot.
- Файлы:
  - `evt_ota_ok__v1.pcm` (OTA downloading succsessfull, rebooting)


#### `VOICE_EVT_OTA_FAIL`
- Когда: OTA upload/запись/финализация не удалась.
- Файлы:
  - `evt_ota_fail__v1.pcm` (OTA fail)


#### `VOICE_EVT_OTA_TIMEOUT`
- Когда: OTA режим истёк по таймауту.
- Файлы:
  - `evt_ota_timeout__v1.pcm` (OTA timer run out, rebooting)


---

### F) Errors / Diagnostics (`/spiffs/voice/error/`)

#### `VOICE_EVT_ERR_GENERIC`
- Когда: общий фейл без классификации.
- Файлы:
  - `evt_err_generic__v1.pcm` (Unspecific and unknown system failure)
  
#### `VOICE_EVT_ERR_STORAGE`
- Когда: файл не найден / SPIFFS не смонтирован / ошибка чтения voice pack.
- Файлы:
  - `evt_err_storage__v1.pcm` (Audio files fail)

#### `VOICE_EVT_ERR_AUDIO`
- Когда: плеер не смог стартовать / invalid state / иные ошибки аудио-пути.
- Файлы:
  - `evt_err_audio__v1.pcm` (Audio player fail. No fucking music anymore)

---

## Замена MVP boot_greeting файлов
Файлы `boot_greeting_*.pcm` считаются MVP и используются только как временные заглушки.
При появлении реального voice pack по стандарту `evt_*__v{1..3}.pcm` заглушки заменяются без сохранения обратной совместимости.

---

## Как добавить новое событие
1) Добавить новый enum в `voice_events.h` (перед `VOICE_EVT__COUNT`).
2) Добавить маппинг evt -> 3 пути в `voice_events.c`.
3) Подготовить 3 PCM файла по стандарту и положить в корректную группу `/spiffs/voice/<group>/`.
4) Обновить этот документ: добавить событие, назначение, пути файлов.

---

## Мини-тест-план (приёмка v1)
1) Для каждого события:
   - варианты v1/v2/v3 проигрываются без повторов,
   - затем мешок сбрасывается и цикл начинается заново (без ошибок).
2) Отсутствующий файл:
   - корректный лог,
   - предсказуемый код ошибки,
   - отсутствие крашей/ребутов.
3) Busy:
   - соблюдается no-interrupt policy,
   - функция возвращает ожидаемую ошибку (или drop, если включено).
4) Mixed soak:
   - серия событий (50+ воспроизведений) не ломает `audio_stream` и не вызывает деградации RX.
5) `BOOT_HELLO` и `SOFT_ON_HELLO` реально различаются (разные файлы / разные фразы).

---

## Будущий шаг: переход на IMA ADPCM (не сейчас)
План: миграция на `IMA ADPCM 4-bit`, язык `en`, 3 файла на событие сохраняются.
Будут уточнены: каталог `/spiffs/voice/en/<group>/`, требования к ADPCM пакету, декодер, валидатор.

## Создание аудиофайлов

1. Общий путь создания аудио файлов:
TTS / нейросеть → MASTER WAV (высокое качество)
                → downsample + mono + normalize
                → PCM raw s16le 16k  → в прошивку сейчас
                → IMA ADPCM 4-bit   → в прошивку позже

2. Требования по файлам:
- WAV
- 24-bit или 16-bit
- 44.1 kHz или 48 kHz
- mono или stereo — не важно (потом сведём)
- Это будет мастер, который ты больше никогда не трогаешь.

Конвертация в PCM s16 mono 16k - ffmpeg