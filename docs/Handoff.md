# HANDOFF — Jinny Lamp (текущее состояние)

Документ “где мы сейчас”, что сделано последней сессией, и как продолжать без разгона.

## 1) Факты по железу (кратко)
- Плата: ReSpeaker XVF3800 с интегрированным XIAO ESP32-S3
- LED: 3×16×16 WS2812B daisy-chain (48×16 = 768)
- DATA: ESP32-S3 GPIO3 → 74AHCT125(5V) → 330–470Ω → матрицы
- Питание матриц: +5V напрямую, GND через low-side N-MOSFET
- Gate MOSFET: XVF3800 GPO X0D11 (Rgate~100Ω, pulldown~100k)

Инвариант WS2812:
- DATA=LOW → MOSFET ON → delay → show
- stop(join) → DATA=LOW → MOSFET OFF

## 2) Факты по софту
- ESP-IDF: v5.5.1
- Flash: 8MB
- PSRAM: 8MB (octal), подтверждено тестом
- Partition table: OTA (ota_0/ota_1) + storage (FS)

Ключевые модули:
- `matrix_anim.*` — единственный show owner (~10 FPS)
- `matrix_ws2812.*` — framebuffer + show
- `fx_engine.* / fx_registry.* / fx_canvas.*` — эффекты
- `ctrl_bus.*` — источник истины состояния
- `j_espnow_link.* + j_espnow_proto.h` — ESPNOW транспорт и протокол
- `ota_portal.*` — SoftAP + HTTP OTA upload

## 3) Что сделано в последней OTA-сессии (факт)
1) OTA “push portal” доведён до рабочего цикла:
   - команда OTA_START (по ESPNOW) переводит лампу в OTA режим
   - SoftAP поднимается (SSID `JINNY-OTA`, IP `192.168.4.1`)
   - HTTP `/update` принимает `.bin`, пишет в OTA partition, делает reboot по успеху
   - добавлен таймаут OTA режима (5 минут) → reboot
2) Исправлена критическая синхронизация остановки анимации:
   - stop выполнен как “stop=join” перед OTA/power-off (без магических delay)
3) Добавлено отображение прогресса загрузки на OTA-странице (UX: видно, что “льётся”)
4) Проверены негативные сценарии:
   - загрузка обрезанного `.bin` корректно завершается `esp_ota_end failed` (HTTP 500)
5) Проверен rollback механизм:
   - искусственный “плохой” билд, который ребутится до mark-valid в pending_verify,
     приводит к загрузке предыдущего OTA слота (rollback работает)
6) Уточнено: anti-rollback выключен, rollback включён (это правильно для разработки)

## 4) Текущая политика связи (важно)
- NORMAL режим: ESPNOW-only, fallback channel = 1, без STA/MQTT (ради стабильности и энергопотребления пульта)
- OTA режим: SoftAP + HTTP upload, активируется по ESPNOW OTA_START

## 5) Как продолжать (что нужно для следующей сессии)
Перед началом работ собрать:
- `git describe --tags --always --dirty`
- `git log -20 --oneline`

Файлы, которые чаще всего нужны:
- `main/main.c`
- `main/ota_portal.c/.h`
- `main/matrix_anim.c/.h`
- `main/j_espnow_link.c/.h`
- `main/j_espnow_proto.h`
- `sdkconfig`
- `partitions.csv` (если менялась таблица)

## 6) Следующие крупные вехи (без детального плана)
- Audio TX (ESP → XVF → speaker) + плеер из FS
- DOA/azimuth чтение и сглаживание (для “джина”)
- Overlay слой (композиция поверх эффектов внутри `matrix_anim`)

