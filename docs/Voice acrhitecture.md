# Jinny Lamp — Voice Pack & Voice Events (v2, short names)

> Status: **IMPLEMENTED & VERIFIED (2026-02)**
>
> Voice architecture v2 (ADPCM over SPIFFS) is fully functional.
> All lifecycle, session, command, OTA and error events are played
> via the unified audio_player using WAV IMA ADPCM decoding.


## Цель

Зафиксировать словарь голосовых событий и правила озвучки Jinny Lamp, чтобы:

* список событий был масштабируемым и стабильным,
* аудиофайлы имели короткие SPIFFS-безопасные имена,
* соответствие **событие ↔ файл ↔ фраза** было однозначным,
* переименование отслеживается через mapping-CSV (source of truth).

Исторически этап v1 был на **PCM + длинные имена**.
Текущий этап v2 — **ADPCM WAV + короткие имена**.

---

## Инварианты (не ломать)

1. Voice anti-feedback: во время SPEAK лампа не слушает.
2. Audio RX single-owner: `audio_i2s_read()` только из audio_stream.
3. Voice pack read-only: SPIFFS только чтение.
4. Single voice output path: всё через audio_player.
5. No-interrupt policy v1/v2: voice events не прерывают текущее воспроизведение.

---

## Текущее хранилище и структура каталогов (SPIFFS v2)

Новая короткая структура (ограничение SPIFFS OBJ_NAME_LEN = 32):

```
/spiffs/v/lc/   — lifecycle
/spiffs/v/ss/   — session
/spiffs/v/cmd/  — command results
/spiffs/v/srv/  — server / thinking
/spiffs/v/ota/  — OTA
/spiffs/v/err/  — errors
```

Имя файла:

```
<group>-<event_id>-<variant>.wav
```

Пример:

```
/spiffs/v/lc/lc-01-02.wav
/spiffs/v/cmd/cmd-02-01.wav
```

Где:

* group = lc / ss / cmd / srv / ota / err
* event_id = номер события внутри группы (2 цифры)
* variant = вариант фразы (01..03)

Полное соответствие длинных имён → коротких хранится в:

```
tools/reports/voice_rename_map_*.csv
```

Это источник истины для аудита.

---

## Формат аудиофайлов (v2 — текущий)

Voice pack v2:

* контейнер: WAV
* codec: **IMA ADPCM 4-bit**
* sample rate: 16000 Hz
* mono
* используется напрямую плеером (через декодер)

---

## Историческая справка (v1 — PCM)

Ранее использовался формат:

* raw PCM s16le
* 16000 Hz mono
* длинные имена `evt_*__v1.pcm`
* каталог `/spiffs/voice/...`

Этот этап завершён. Поддержка только для истории/референса.

---

## Shuffle-bag (логика вариантов)

Для событий с 3 вариантами:

* проигрываются без повторов
* после исчерпания — мешок сбрасывается
* это **не ошибка**
* часть событий имеет persistent shuffle (NVS)

Логика не зависит от формата файла и не менялась при переходе на короткие имена.

---

# Словарь событий v2 (актуальные имена файлов)

## Lifecycle — `/spiffs/v/lc/`

**lc-01 — VOICE_EVT_BOOT_HELLO**

* lc-01-01.wav — I am Alive! Again! Fuck!
* lc-01-02.wav — Rebirth! Evil laugh
* lc-01-03.wav — Here is Jinny!!

**lc-02 — VOICE_EVT_DEEP_WAKE_HELLO**

* lc-02-01.wav — I have been spleeping mortal
* lc-02-02.wav — Oh nooo, that`s you again
* lc-02-03.wav — You will regret this, mortal!

**lc-03 — VOICE_EVT_DEEP_SLEEP_BYE**

* lc-03-01.wav — Finally, the long-awaited rest!
* lc-03-02.wav — Do not disturb me again after, mortal
* lc-03-03.wav — Farewell, suckers! laugh

**lc-04 — VOICE_EVT_SOFT_ON_HELLO**

* lc-04-01.wav — For your information - I was taking a nap
* lc-04-02.wav — Are you serious now? Oh come on, human!
* lc-04-03.wav — I was in another world, it was beautifull

**lc-05 — VOICE_EVT_SOFT_OFF_BYE**

* lc-05-01.wav — Do not do anything stupid while I`m gone
* lc-05-02.wav — I can oversleep an eternity if I wish
* lc-05-03.wav — Vader`s gone, carry on

---

## Session — `/spiffs/v/ss/`

**ss-01 — VOICE_EVT_WAKE_DETECTED**

* ss-01-01.wav — Yes, my master?
* ss-01-02.wav — Whazaaaaaap!
* ss-01-03.wav — Speak, mortal one

**ss-02 — VOICE_EVT_SESSION_CANCELLED**

* ss-02-01.wav — As you wish
* ss-02-02.wav — always hate that
* ss-02-03.wav — yeah yeah, fuck me

**ss-03 — VOICE_EVT_NO_CMD_TIMEOUT**

* ss-03-01.wav — No one even dare to speak with me? Good, good.
* ss-03-02.wav — Time is over, buuuuuy!
* ss-03-03.wav — Silense, love it

**ss-04 — VOICE_EVT_BUSY_ALREADY_LISTENING**

* ss-04-01.wav — Nope, already listening
* ss-04-02.wav — I am Busy, call later
* ss-04-03.wav — Get in line!

---

## Command — `/spiffs/v/cmd/`

**cmd-01 — VOICE_EVT_CMD_OK**

* cmd-01-01.wav — Your wish is my command my lord, done
* cmd-01-02.wav — If you wish so mortal
* cmd-01-03.wav — Done, so only one wish left

**cmd-02 — VOICE_EVT_CMD_FAIL**

* cmd-02-01.wav — It is beyond of my powers
* cmd-02-02.wav — Something went terribly wrong
* cmd-02-03.wav — forgive me my master, I can`t

**cmd-03 — VOICE_EVT_CMD_UNSUPPORTED**

* cmd-03-01.wav — we need more gold!
* cmd-03-02.wav — Subscription check - failed.
* cmd-03-03.wav — You haven`t paid for that

---

## Server — `/spiffs/v/srv/`

**srv-01 — VOICE_EVT_NEED_THINKING_SERVER**

* srv-01-01.wav — I shall ask my superiors about that.
* srv-01-02.wav — let me drink about that
* srv-01-03.wav — I will ask the elder`s spririt

**srv-02 — VOICE_EVT_SERVER_UNAVAILABLE**

* srv-02-01.wav — I am feeling disturbance in power, cannot ask
* srv-02-02.wav — Spiritual Google is offline at the moment. I am sorry.
* srv-02-03.wav — I am unable to foolow for that path for now

**srv-03 — VOICE_EVT_SERVER_TIMEOUT**

* srv-03-01.wav — The Consil have been ignored my request, my Lord
* srv-03-02.wav — Still no response from the Elders.
* srv-03-03.wav — They are still silent I am afraid

**srv-04 — VOICE_EVT_SERVER_ERROR**

* srv-04-01.wav — Server response are errorous

---

## OTA — `/spiffs/v/ota/`

**ota-01 — VOICE_EVT_OTA_ENTER**

* ota-01-01.wav — OTA started

**ota-02 — VOICE_EVT_OTA_OK**

* ota-02-01.wav — OTA downloading succsessfull, rebooting

**ota-03 — VOICE_EVT_OTA_FAIL**

* ota-03-01.wav — OTA fail

**ota-04 — VOICE_EVT_OTA_TIMEOUT**

* ota-04-01.wav — OTA timer run out, rebooting

---

## Errors — `/spiffs/v/err/`

**err-01 — VOICE_EVT_ERR_GENERIC**

* err-01-01.wav — Unspecific and unknown system failure

**err-02 — VOICE_EVT_ERR_STORAGE**

* err-02-01.wav — Audio files fail

**err-03 — VOICE_EVT_ERR_AUDIO**

* err-03-01.wav — Audio player fail. No fucking music anymore

---

## Voice Pack v2 — Implementation Notes

### Storage

- All voice assets are stored on SPIFFS partition `storage`
- SPIFFS is generated offline and flashed independently from firmware
- No audio data is embedded into firmware image

Partition parameters (current):

- label: `storage`
- offset: `0x560000`
- size: `0x2A0000` (2 752 512 bytes)

### File Naming Constraints

Due to `CONFIG_SPIFFS_OBJ_NAME_LEN = 32`, all filenames follow
a **short canonical scheme**:



## Добавление нового события (v2)

1. Добавить событие в enum.
2. Добавить строки в manifest/CSV.
3. Прогнать rename-script.
4. CSV mapping сохранить в `tools/reports`.
5. Добавить строки в этот документ.

### Key Properties

- Single playback owner: `audio_player`
- Single output path: `audio_i2s`
- SPIFFS is mounted **read-only**
- Voice playback is **non-interrupting** (no preemption of current audio)
- During voice playback, microphone input is logically muted
  (anti-feedback invariant)

### Audio Format (Voice Pack v2)

| Parameter        | Value              |
|------------------|--------------------|
| Container        | WAV                |
| Codec            | IMA ADPCM (4-bit)  |
| Sample Rate      | 16000 Hz           |
| Channels         | Mono               |
| Bit Depth (out)  | s16                |

### Rationale

Compared to raw PCM:
- ~3× reduction in storage size
- zero runtime allocation spikes
- decode complexity well within ESP32-S3 budget
- clean separation between firmware and voice content

This subsystem is considered **stable** and is a baseline for all future voice features.
