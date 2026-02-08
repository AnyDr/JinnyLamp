
# ТЗ: Voice Events v1 + Voice Pack (PCM) для Jinny Lamp

## 0) Цель

Довести систему голосовых событий (“словарь событий + правила озвучки”) до уровня, близкого к финальному, **на PCM**, без “глюков” и без усложнений (MultiNet и ADPCM позже отдельным шагом).

Результат должен обеспечить:

* масштабируемый список событий (enum/table-driven),
* понятную структуру файлов в `/spiffs/voice/...`,
* стабильный shuffle-bag на 3 варианта без повторов и без ошибок “варианты кончились”,
* ясную документацию по добавлению новых событий и новых аудиофайлов.

---

## 1) Инварианты (не ломать)

1. **Voice anti-feedback:** во время SPEAK лампа не слушает (wake/ASR выключены).
2. **Audio RX single-owner:** `audio_i2s_read()` вызывается только из `audio_stream.c`.
3. **Storage voice policy:** voice pack хранится в SPIFFS `/spiffs` и используется **read-only** в рантайме.
4. **Single voice output path:** воспроизведение делается через `audio_player` (единый путь).
5. **No interruption policy (v1):** голосовые события **не прерывают** текущее воспроизведение. Если плеер занят — применяется политика drop/error (см. ниже), но без interrupt.

---

## 2) Scope / вне рамок (явно)

* **MultiNet**: не внедряется, только готовим события, которые потом будут использоваться.
* **ADPCM (IMA ADPCM 4-bit)**: не внедряется сейчас. Документация должна предусмотреть будущий переход, но текущий формат — PCM.
* Серверный fallback (LLM/bridge) — только на уровне событий, без реализации сети.

---

## 3) Состояние хранения и структура каталогов (SPIFFS)

Текущий корень: `/spiffs/voice`

На этапе PCM используем группировку по смыслу (человекочитаемо, удобно для поддержки):

* `/spiffs/voice/lifecycle/`
* `/spiffs/voice/session/`
* `/spiffs/voice/cmd/`
* `/spiffs/voice/server/`
* `/spiffs/voice/ota/`
* `/spiffs/voice/error/`

Языковой подкаталог (`/en/`) добавим при переходе на ADPCM (или позже), сейчас не обязателен.

---

## 4) Формат аудиофайлов (PCM, текущий этап)

**Единый стандарт для всех voice event файлов:**

* Raw PCM (без WAV заголовка)
* 16 kHz sample rate
* Mono (1 channel)
* Signed 16-bit little-endian (s16 LE)

Рекомендации контента:

* избегать клиппинга (peak ≤ -1 dBFS),
* короткие фразы/сигналы: обычно 0.3–1.5 s, системные сообщения до 2–3 s (по вкусу),
* без длинных “хвостов тишины”.

---

## 5) Нейминг файлов (строгий стандарт)

Каждый voice event имеет **до 3 вариантов** (`v1..v3`).

Шаблон имени:
`evt_<snake_case_event>__v{1..3}.pcm`

Полный путь:
`/spiffs/voice/<group>/evt_<event>__v{1..3}.pcm`

Примеры:

* `/spiffs/voice/lifecycle/evt_boot_hello__v1.pcm`
* `/spiffs/voice/lifecycle/evt_soft_on_hello__v2.pcm`
* `/spiffs/voice/session/evt_session_cancelled__v3.pcm`
* `/spiffs/voice/cmd/evt_cmd_ok__v1.pcm`
* `/spiffs/voice/server/evt_server_timeout__v2.pcm`
* `/spiffs/voice/ota/evt_ota_fail__v3.pcm`
* `/spiffs/voice/error/evt_err_audio__v1.pcm`

---

## 6) Shuffle-bag (3 варианта) — обязательная логика

Для каждого события поддерживается shuffle-bag по вариантам v1..v3.

Правила:

1. Пока не проиграны все варианты — выбираем случайный из ещё не проигранных.
2. Когда все 3 варианта проиграны — **мешок сбрасывается** и цикл начинается заново.
3. Исчерпание вариантов **не является ошибкой**. Никогда не должно приводить к отказу/“самоуничтожению”.
4. В пределах одной wake-session желательно избегать повторов, но после сброса мешка варианты снова доступны.

Persistency:

* Для части событий мешок хранится **persistently** (NVS) и переживает reboot (см. ниже).
* Для остальных — RAM-only и сбрасывается на reboot.

---

## 7) Политика busy (плеер занят)

Система моноголосая, прерываний нет.

Если в момент `voice_event_post()` плеер занят:

* для “мелких” событий допускается **drop** (в дальнейшем флагом `drop_if_busy`),
* для “важных” событий возвращаем ошибку наверх (FSM решает, что делать).

На v1 допустима простая реализация:

* если busy → вернуть `ESP_ERR_INVALID_STATE` и залогировать (или drop для выбранных событий).

---

## 8) Словарь событий (v1) и группировка (C-style имена)

События должны быть реализованы как enum + таблица `evt -> (path1,path2,path3)`.

### A) Lifecycle / Power

1. `VOICE_EVT_BOOT_HELLO`
   Cold boot (первый старт после питания/ресета). **Отдельно от soft on.**

2. `VOICE_EVT_DEEP_WAKE_HELLO`
   Выход из deep sleep (локально).

3. `VOICE_EVT_DEEP_SLEEP_BYE`
   Вход в deep sleep (локально).

4. `VOICE_EVT_SOFT_ON_HELLO`
   Выход из SOFT OFF (включили по пульту/серверу).

5. `VOICE_EVT_SOFT_OFF_BYE`
   Вход в SOFT OFF (выключили по пульту/серверу).

Persistency (рекомендация v1):

* persistent shuffle для: `BOOT_HELLO`, `DEEP_WAKE_HELLO`, `DEEP_SLEEP_BYE`
* остальные RAM-only (или тоже persistent, если нужно по UX; решение фиксируется в коде и доке)

### B) Session / Wake

6. `VOICE_EVT_WAKE_DETECTED`
   WakeNet сработал.

7. `VOICE_EVT_SESSION_CANCELLED`
   Явная отмена сессии голосом (sleep-word/cancel).

8. `VOICE_EVT_NO_CMD_TIMEOUT`
   Wake был, но команда не пришла в окно (например 8 секунд).

9. `VOICE_EVT_BUSY_ALREADY_LISTENING`
   Повторный wake при уже активной сессии.

### C) Command outcome (локальные подтверждения действий)

10. `VOICE_EVT_CMD_OK`
11. `VOICE_EVT_CMD_FAIL`
12. `VOICE_EVT_CMD_UNSUPPORTED`

*(Конкретизация по яркости/скорости/анимации пока не требуется; достаточно общего outcome.)*

### D) Server / LLM flow (семантика, реализация позже)

13. `VOICE_EVT_NEED_THINKING_SERVER`
    Локально не распознано, сервер доступен, отправляем запрос.

14. `VOICE_EVT_SERVER_UNAVAILABLE`
    Сервер недоступен (нет связи).

15. `VOICE_EVT_SERVER_TIMEOUT`
    Ответ не пришёл за `SERVER_WAIT_MAX_MS`.

16. `VOICE_EVT_SERVER_ERROR`
    Сервер вернул ошибку/невалидный ответ.

### E) OTA lifecycle

17. `VOICE_EVT_OTA_ENTER`
18. `VOICE_EVT_OTA_OK`
19. `VOICE_EVT_OTA_FAIL`
20. `VOICE_EVT_OTA_TIMEOUT`

### F) Errors / Diagnostics

21. `VOICE_EVT_ERR_GENERIC`
22. `VOICE_EVT_ERR_STORAGE`
23. `VOICE_EVT_ERR_AUDIO`

---

## 9) Требования к масштабируемости (чтобы не было скрытых ограничений)

* В enum обязателен элемент `VOICE_EVT__COUNT` (последним).
* Любые массивы/маски в RAM должны иметь размер `VOICE_EVT__COUNT` (никаких “16”).
* Таблица маппинга должна быть полной (на каждый evt 3 строки/пути или NULL).

---

## 10) Замена MVP boot_greeting файлов

Текущие `boot_greeting_*.pcm` считаются MVP и подлежат замене на файлы согласно новому стандарту, как только появляется готовый набор файлов.

---

## 11) Документация

Вся документация по событиям/голосу/форматам/неймингу хранится в:

* `docs/voice_pack.md`

В `architecture.md`/`handoff.md` допускается только короткая ссылка/резюме (без дублирования).

---

## 12) “Как добавить новое событие” (процесс)

1. Добавить новый элемент в enum (до `VOICE_EVT__COUNT`).
2. Добавить маппинг `evt -> 3 файла` (пути по стандарту).
3. Подготовить 3 PCM файла в нужном каталоге.
4. Обновить `docs/voice_pack.md`: добавить событие в список и назначение.

---

## 13) Мини-тест-план (приёмка v1)

* Для каждого события: проигрываются варианты v1/v2/v3 без повторов, затем цикл повторяется снова (сброс мешка).
* Отсутствующий файл: корректный лог + предсказуемый код ошибки (без крашей).
* Busy: подтверждённая политика (drop/error), без interrupt и без зацикливания.
* Серия воспроизведений (mixed events) не ломает `audio_stream` и не вызывает деградации RX.
* Cold boot event и Soft ON event различаются (разные файлы).