# Jinny Lamp — Tech debt (актуально)

Правило: не “перестраиваем всё”, фиксируем точечно, с критериями приёмки.

## DONE (закрыто)
- OTA SoftAP portal по ESPNOW `OTA_START` (upload/reboot/timeout)
- DOA v1: чтение AEC_AZIMUTH_VALUES auto-beam + debug FX/лог (как опция)

## P0
### P0.1 Safe shutdown единым helper везде
Цель: один путь `stop(join) → DATA=LOW → MOSFET OFF` используется в:
- power off
- deep sleep
- OTA start / OTA timeout / OTA error


Критерий: нет артефактов WS2812, нет гонок по show.

### P0.2 “Питание матрицы через XVF” не должно валить систему
Причина: transient ESP_FAIL на старте I2C/XVF.
Критерий: при ошибке power-on лампа живёт (ESPNOW/DOA), матрица безопасно выключена.

## P1
### P1.1 Pause semantics без ресета
paused=1 замораживает фазу, resume продолжает без перезапуска seed/таймера.

### P1.2 DOA upgrade до “processed azimuth”
Перейти на `AUDIO_MGR_SELECTED_AZIMUTHS` (NaN=нет речи) и `AEC_SPENERGY_VALUES` (envelope attack/release).
Критерий: стабильный UI-угол без “нервности”, корректное затухание при тишине.

## P2
- I2S TX + плеер фраз из FS
- wake word / команды / voice state machine
- Genie overlay (LISTEN/SPEAK) без нарушения single-show-owner

## NEW (2026-01-29) — найденные косяки / риски (держим под контролем)

### P0.3 I2S duplex: tx_set_enabled() ломает RX (тайм:contentReference[oaicite:5]{index=5}ия)
Симптом:
- При старте проигрывания: `i2s_channel_enable ... already enabled` (TX уже включен в audio_i2s_init).
- После `AUDIO_PLAYER: play done` начинают сыпаться `AUDIO_STREAM: audio_i2s_read err=ESP_ERR_TIMEOUT`.

Причина:
- В ESP-IDF full-duplex RX/TX share BCLK/WS; `i2s_channel_disable(tx)` может остановить клоки и тем самым “убить” RX чтение.

Fix (минимально-инвазивно):
- Сделать `audio_i2s_tx_set_enabled(true)` идемпотентным (already enabled => ESP_OK).
- `audio_i2s_tx_set_enabled(false)` не должен дергать `i2s_channel_disable(tx)` в одиночку; вместо этого no-op (опц. протолкнуть тишину).

Критерий:
- нет `already enabled` ошибок,
- после play done RX продолжает выдавать данные, нет лавины таймаутов.

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