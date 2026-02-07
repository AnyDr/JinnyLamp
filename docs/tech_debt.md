# Jinny Lamp — Tech debt (актуально)



Fix (минимально-инвазивно):
- Сделать `audio_i2s_tx_set_enabled(true)` идемпотентным (already enabled => ESP_OK).
- `audio_i2s_tx_set_enabled(false)` не должен дергать `i2s_channel_disable(tx)` в одиночку; вместо этого no-op (опц. протолкнуть тишину).

Критерий:
- нет `already enabled` ошибок,
- после play done RX продолжает выдавать данные, нет лавины таймаутов.

### Апдейт 2026-02-02 (статус)
- Ранний playback до готовности TX (ESP_ERR_INVALID_STATE / channel not enabled) устранён:
  - добавлен I2S ready flag после enable TX+RX,
  - в audio_player включён best-effort enable TX перед play.
- Tail/flush механизм (тишина после play) не изменяли.

### P1.x main.c: мелкие “шероховатости”, не трогаем сейчас
- Дубли include’ов (шум/хрупкость зависимостей).
- В deep sleep вызывается `matrix_anim_stop_and_wait()` даже если anim не был стартован (нужна защита “started?”).
- Много `ESP_ERROR_CHECK()` до “режима выживания”: потенциально валит систему при transient ошибках (противоречит цели P0.2).
- Авто-проигрывание тестового тона в app_main может маскировать/усиливать гонки по I2S и ресурсам, мешает диагностике.
- audio_tone_test.c: хрупкий include (esp_err_to_name/esp_err.h) — может собираться случайно через чужие хедеры.

## Power / Sleep Technical Debt

- [ ] Separate SOFT OFF logic from deep sleep logic in main.c
- [ ] Rename internal flags to avoid semantic confusion (OFF vs SLEEP)
- [ ] Ensure goodbye voice event is bound to SOFT OFF, not deep sleep
- [ ] Audit all shutdown paths for accidental esp_deep_sleep_start()
- [ ] Document button gestures: SOFT OFF vs DEEP SLEEP
- [ ] Align docs with current POWER v1 semantics (paused+brightness=0) vs target SOFT OFF (MOSFET OFF)
- [ ] Decide whether to extend ACK with explicit power_state (optional) or keep it purely derived from MOSFET/anim state


---------------------------------------------------------------------------------
Раздел storage будет заполнен почти под завязку, это для SPIFFS риск. Поэтому стратегия такая:

в рантайме не пишем/не удаляем файлы (только чтение).

обновление “голосов” делаем прошивкой образа storage (одним write_flash), либо через твой OTA-портал отдельным механизмом позже (но это уже отдельная история).
-----------------------------------------------------------------------------------

### Voice / Wake — Technical Debt

- Заменить временные PCM-заглушки (boot_greeting) на:
  - thinking_*.pcm
  - no_cmd_*.pcm
- Ввести VOICE_EVT__COUNT в voice_events.h и убрать VOICE_EVT_RAM_COUNT
- Формализовать завершение wake-session по распознанной команде
- Расширить Genie overlay:
  - анимация вместо одиночной точки
  - визуальная индикация “слушаю / думаю / отвечаю”
