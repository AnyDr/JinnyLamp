# ASR Commands v1

**Project:** Jinny Lamp
**Stack:** ESP-SR (WakeNet + MultiNet), English
**Status:** Design-frozen for v1

---

## 1. Назначение документа

Этот документ фиксирует **список ASR-команд (интентов)**, которые может распознать MultiNet, и их связь с:

* доменными действиями системы (ctrl_bus / lifecycle / OTA / server),
* голосовыми ответами лампы (voice events).

Документ является **контрактом** между:

* ASR (MultiNet),
* логикой приложения (router),
* voice pack (audio events).

---

## 2. Базовые принципы

1. **ASR Command ≠ Voice Event**
   ASR-команда — это *что пользователь сказал*.
   Voice event — это *что лампа ответила*.

2. **MultiNet ничего не “делает”**
   Он только выдаёт `asr_intent_id + confidence`.

3. **Вся логика в router’е**

   * проверка допустимости команды,
   * выполнение действия,
   * выбор voice event (OK / FAIL / UNSUPPORTED / SPECIAL).

4. **MultiNet активен только после wake word**

   * старт: wake word,
   * стоп: команда / cancel / sleep / timeout.

---

## 3. Жизненный цикл ASR-сессии (кратко)

```
IDLE
 └─ WakeNet ON

wake word →
 └─ WakeNet OFF
 └─ MultiNet ON
 └─ ASR session timer started

ASR command →
 └─ router handles intent
 └─ MultiNet OFF
 └─ SPEAK (voice event)
 └─ return to IDLE
```

---

## 4. Список ASR Commands v1

### A. Session / Dialog control

| ASR Intent ID | Example phrases                                       | Action           | Voice Event                   | Notes               |
| ------------- | ----------------------------------------------------- | ---------------- | ----------------------------- | ------------------- |
| `ASR_CANCEL`  | “cancel”, “disregard”, “never mind”, “stop listening” | End ASR session  | `VOICE_EVT_SESSION_CANCELLED` | Не трогает ctrl_bus |
| `ASR_SLEEP`   | “go to sleep”, “sleep now”, “good night”              | Enter deep sleep | `VOICE_EVT_DEEP_SLEEP_BYE`    | Lifecycle-команда   |

---

### B. Power / Pause

| ASR Intent ID   | Example phrases                       | Action         | Voice Event               | Notes          |
| --------------- | ------------------------------------- | -------------- | ------------------------- | -------------- |
| `ASR_POWER_OFF` | “turn off”, “lights off”, “power off” | Soft-off       | `VOICE_EVT_SOFT_OFF_BYE`  | Без deep sleep |
| `ASR_POWER_ON`  | “turn on”, “wake up”, “lights on”     | Soft-on        | `VOICE_EVT_SOFT_ON_HELLO` |                |
| `ASR_PAUSE`     | “pause”, “freeze”                     | paused = true  | `VOICE_EVT_CMD_OK / FAIL` |                |
| `ASR_RESUME`    | “resume”, “continue”                  | paused = false | `VOICE_EVT_CMD_OK / FAIL` |                |

---

### C. Animation control

| ASR Intent ID    | Example phrases                           | Action        | Voice Event                   | Notes               |
| ---------------- | ----------------------------------------- | ------------- | ----------------------------- | ------------------- |
| `ASR_NEXT_ANIM`  | “next”, “next animation”, “change effect” | effect_id++   | `CMD_OK / FAIL / UNSUPPORTED` |                     |
| `ASR_PREV_ANIM`  | “previous”, “go back”                     | effect_id--   | `CMD_OK / FAIL / UNSUPPORTED` |                     |
| `ASR_SET_ANIM_N` | “animation five”, “effect ten”            | effect_id = N | `CMD_OK / FAIL / UNSUPPORTED` | Ограничить диапазон |

---

### D. Brightness & speed

| ASR Intent ID     | Example phrases             | Action             | Voice Event     |
| ----------------- | --------------------------- | ------------------ | --------------- |
| `ASR_BRIGHT_UP`   | “brighter”, “brightness up” | brightness += step | `CMD_OK / FAIL` |
| `ASR_BRIGHT_DOWN` | “dimmer”, “brightness down” | brightness -= step | `CMD_OK / FAIL` |
| `ASR_SPEED_UP`    | “faster”, “speed up”        | speed_pct += step  | `CMD_OK / FAIL` |
| `ASR_SPEED_DOWN`  | “slower”, “speed down”      | speed_pct -= step  | `CMD_OK / FAIL` |

---

### E. Volume control

| ASR Intent ID     | Example phrases          | Action         | Voice Event     |
| ----------------- | ------------------------ | -------------- | --------------- |
| `ASR_VOLUME_UP`   | “volume up”, “louder”    | volume += step | `CMD_OK / FAIL` |
| `ASR_VOLUME_DOWN` | “volume down”, “quieter” | volume -= step | `CMD_OK / FAIL` |
| `ASR_MUTE`        | “mute”                   | volume = 0     | `CMD_OK / FAIL` |
| `ASR_UNMUTE`      | “unmute”                 | restore volume | `CMD_OK / FAIL` |

---

### F. OTA

| ASR Intent ID   | Example phrases                                | Action              | Voice Event                                 |
| --------------- | ---------------------------------------------- | ------------------- | ------------------------------------------- |
| `ASR_OTA_START` | “start update”, “start OTA”, “update firmware” | Start OTA lifecycle | `VOICE_EVT_OTA_ENTER` → OK / FAIL / TIMEOUT |

---

### G. Server bridge (placeholder)

| ASR Intent ID    | Example phrases                                | Action         | Voice Event                      | Notes                   |
| ---------------- | ---------------------------------------------- | -------------- | -------------------------------- | ----------------------- |
| `ASR_ASK_SERVER` | “ask the server”, “ask the council”, “help me” | Server request | `VOICE_EVT_NEED_THINKING_SERVER` | Реализация через bridge |

---

## 5. Special outcomes (не ASR-команды)

Эти события **не распознаются** MultiNet напрямую, но обязательны для логики:

| Condition            | Action      | Voice Event                        |
| -------------------- | ----------- | ---------------------------------- |
| No ASR result        | End session | `VOICE_EVT_NO_CMD_TIMEOUT`         |
| Busy / invalid state | Reject      | `VOICE_EVT_BUSY_ALREADY_LISTENING` |
| Command unsupported  | Reject      | `VOICE_EVT_CMD_UNSUPPORTED`        |

---

## 6. Что сознательно НЕ входит в v1

* Свободный NLU / произвольные фразы
* Голосовой выбор цвета / сложные параметры
* Прерывание текущего SPEAK
* Постоянно активный MultiNet
* Deep sleep через сервер

Это может быть добавлено позже без ломки контракта.
