# HANDOFF — Jinny Lamp (текущее состояние)

## 1) Железо (кратко)
- ReSpeaker XVF3800 + XIAO ESP32-S3
- 3×16×16 WS2812B daisy-chain (48×16)
- DATA: GPIO3 → 74AHCT125(5V) → 330–470Ω → матрицы
- Питание матриц: +5V напрямую, GND через low-side N-MOSFET
- Gate MOSFET: XVF GPO X0D11 (pin=11)

Инвариант WS2812:
- ON: DATA=LOW → MOSFET ON → delay → show
- OFF: stop(join) → DATA=LOW → MOSFET OFF

## 2) Софт / модули
ESP-IDF v5.5.1, Flash 8MB, PSRAM 8MB, OTA layout.

Ключевые модули:
- `matrix_anim.*` — единственный show owner (~10 FPS)
- `matrix_ws2812.*` — framebuffer + show
- `fx_engine.* / fx_registry.* / fx_canvas.*` — эффекты
- `ctrl_bus.*` — источник истины состояния
- `j_espnow_link.* + j_espnow_proto.h` — ESPNOW транспорт/протокол
- `ota_portal.*` — SoftAP + HTTP OTA upload
- `doa_probe.*` — DOA always-on snapshot

## 3) Статус OTA (факт)
- Вход по ESPNOW `OTA_START`
- SoftAP SSID `JINNY-OTA`, gateway `192.168.4.1`
- `/update` принимает `.bin`, пишет OTA slot, reboot по успеху
- timeout 300s

Rollback включён, anti-rollback выключен.

## 4) Статус DOA (факт)
- DOA читается с XVF по I2C: `AEC_AZIMUTH_VALUES` (resid=33 cmd=75 len=16)
- используем `[3] auto-selected beam`, float32 LE radians → degrees
- сервис DOA always-on, debug FX/лог включаются отдельно

## 5) Следующий шаг (если продолжать голос)
- I2S TX (ESP → XVF) + плеер из FS
- wake word + voice state machine
- Genie overlay (LISTEN/SPEAK) внутри одного кадра matrix_anim
