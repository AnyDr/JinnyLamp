# Архитектура Jinny Lamp

Карта модулей и потоков данных: кто кого вызывает, какие задачи, какие инварианты.

## 0) High level

Jinny Lamp — прошивка лампы (ESP32-S3 host) на плате ReSpeaker XVF3800 (voice DSP) с адресными WS2812 матрицами.
Лампа является **источником истины** состояния (effect/brightness/speed/paused/power/state_seq). Пульт — клиент: шлёт команды и обновляет UI по ACK snapshot.

Каналы управления:
- локально: сенсорная кнопка TTP223 (поллинг),
- удалённо: ESP-NOW (основной канал),
- OTA: локальный SoftAP + HTTP upload (активируется командой OTA_START по ESP-NOW).

### Ключевые инварианты
1) **Single show owner:** только `matrix_anim` имеет право вызывать `matrix_ws2812_show()`.
2) **WS2812 power sequencing:**
   - Включение: DATA=LOW → MOSFET ON → задержка → show
   - Выключение: stop anim (join) → DATA=LOW → MOSFET OFF
3) **OTA safety:** перед OTA или power-off анимация должна быть остановлена синхронно (join), без “угадываний delay”.

---

## 1) Подсистемы

## 1.1 LED pipeline (render → show)
- `matrix_anim` (FreeRTOS task, ~10 FPS)
  - вызывает `fx_engine_render(t_ms)` → рисует в canvas
  - вызывает `matrix_ws2812_show()` → единственный push на WS2812

Композиция (будущая идея “джин поверх эффекта”) допускается только как post-pass в рамках кадра `matrix_anim`,
без второго task и без второго show.

## 1.2 FX engine и registry
- `fx_registry`: таблица эффектов (id, name, base_step, render_cb)
- `fx_engine`: держит текущий effect_id и параметры, вызывает render с учётом времени и speed_pct

Важный контракт: SET_ANIM корректен только при совпадении `effect_id` между пультом и лампой
(или при наличии протокола синхронизации списка эффектов — это отдельная задача).

## 1.3 Состояние управления (ctrl_bus)
`ctrl_bus` — single source of truth:
- `effect_id` (uint16)
- `brightness` (uint8, 0..255)
- `speed_pct` (uint16, 10..300)
- `paused` (bool)
- `power` (bool, если используется)
- `state_seq` (uint32, монотонный счётчик применений)

Источники команд:
- ESPNOW RX (пульт),
- TTP223 (локально),
- (позже) голос/DOA.

## 1.4 Wireless: ESPNOW-only режим (текущая политика)
В NORMAL режиме:
- Wi-Fi STA не используется (SSID пустой / не подключаемся к роутеру).
- ESPNOW работает на фиксированном fallback канале (по Kconfig), сейчас используется `ch=1`.

В OTA режиме:
- лампа поднимает SoftAP (SSID/PASS), стартует HTTP OTA портал и принимает firmware .bin по Wi-Fi (локально).

## 1.5 OTA подсистема (SoftAP + HTTP upload)
Компоненты:
- `ota_portal` — владеет жизненным циклом OTA:
  - stop anim (join)
  - “make LEDs safe”: DATA=LOW, MOSFET OFF
  - SoftAP + httpd endpoint `/update`
  - запись в OTA partition (`ota_0`/`ota_1`)
  - reboot после успеха
  - таймаут (напр. 5 минут) → reboot (если не было успешного завершения)

Rollback:
- включён `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` и `CONFIG_APP_ROLLBACK_ENABLE`
- anti-rollback выключен (`CONFIG_*_ANTI_ROLLBACK is not set`)
- после успешного старта нового слота прошивка должна вызвать mark-valid (отложенно, чтобы “система жива”)

## 1.6 Audio / XVF3800 (I2C + I2S)
- I2C: управление XVF и его GPIO/GPO (в т.ч. MOSFET gate на X0D11)
- I2S: аудио RX/TX (RX уже поднят; TX/плеер из FS — отдельная задача)
- `asr_debug` — диагностическая задача (опциональна; может давать шум/таймауты при смене режимов)

---

## 2) Потоки данных

### 2.1 Управление светом (ESPNOW)
1) Remote → ESPNOW CTRL → Lamp
2) Lamp: `j_espnow_link` RX → `ctrl_bus_submit()`
3) Lamp: отправляет ACK snapshot обратно на Remote
4) `matrix_anim` периодически рендерит `fx_engine` и делает show

### 2.2 OTA (ESPNOW → SoftAP → upload)
1) Remote → ESPNOW `OTA_START` → Lamp
2) Lamp: останавливает анимацию (join), делает LEDs safe, поднимает SoftAP + OTA портал
3) Клиент (телефон/ноут) подключается к SSID и загружает `.bin` на `/update`
4) Lamp: `esp_ota_end` проверяет образ, при успехе: set_boot_partition + reboot
5) Новый слот стартует, после “окна живучести” вызывается mark-valid; иначе rollback

---

## 3) Задачи/владение ресурсами (тезисно)
- `matrix_anim`: единственный владелец show (WS2812)
- `ota_portal`: единственный владелец OTA lifecycle (Wi-Fi AP + httpd + esp_ota_*)
- `ctrl_bus`: единственный владелец “состояния”
- `j_espnow_link`: транспорт RX/TX команд и ACK

