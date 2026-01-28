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
