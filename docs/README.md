# Jinny Lamp (ESP32-S3 + XVF3800 + WS2812)

## Что это
Jinny Lamp - прошивка для умной лампы на базе XIAO ESP32-S3, установленного на плате ReSpeaker XVF3800 (DSP/voice).
Лампа управляет адресными RGB матрицами WS2812 (3×16×16, итого 48×16 = 768 LED), умеет проигрывать эффекты и принимать команды управления по ESP-NOW.

Ключевая цель проекта: надёжность, повторяемость, минимум “магии”, понятная модульная архитектура.
Стратегия связи: ESPNOW сейчас, Wi-Fi STA/MQTT позже (один радиомодуль, общий канал).

## Аппаратная часть (кратко)
- MCU: ESP32-S3 (XIAO, интегрирован на плате XVF3800)
- LED: 3× WS2812B 16×16 daisy-chain, раскладка “змейкой”
- DATA: GPIO3 ESP32-S3 -> level shifter 74AHCT125 (5V) -> серийный резистор 330–470 Ω -> матрицы
- Питание матриц: +5V напрямую, GND коммутируется low-side N-MOSFET
- Gate MOSFET управляется XVF3800 GPO (X0D11), с Rgate ~100 Ω и pulldown ~100 kΩ
- Инвариант включения WS2812: DATA=LOW -> MOSFET ON -> задержка -> отправка данных
- Инвариант выключения WS2812: stop anim -> DATA=LOW -> MOSFET OFF

## Что сейчас работает (факт по текущему состоянию)
- Движок эффектов и рендер (fx_engine + fx_registry + fx_canvas)
- Анимационная задача (matrix_anim)
- Управление состоянием (ctrl_bus): effect_id / brightness / speed_pct / pause / power
- ESP-NOW управление от пульта: power / pause / brightness / speed_pct
  - SET_ANIM поддержан протоколом, но корректность зависит от совпадения таблицы effect_id на пульте и fx_registry лампы
- Wi-Fi STA поднят как радио-база (SSID может быть пустым для ESPNOW-only)
- Отладка аудио тракта (asr_debug) может шуметь логами (см. docs/architecture.md и docs/commands.md)

## Быстрый старт (сборка/прошивка)
ESP-IDF: v5.5.1

Типовой цикл:
- `idf.py set-target esp32s3`
- `idf.py build`
- `idf.py -p COMxx flash`
- `idf.py -p COMxx monitor`

## Документация (куда смотреть)
- `docs/Handoff.md` - “что за проект и где продолжать” (самое важное)
- `docs/architecture.md` - модули, потоки данных, задачи, порядок инициализации, инварианты
- `docs/espnow.md` - ESP-NOW: канал, pairing по MAC, команды, ACK, диагностика
- `docs/commands.md` - команды разработчика и семантика управляющих команд
- `docs/fx_fire.md` - документация сложного эффекта FIRE (по факту файла fx_effects_fire.c)
- `docs/adr/` - ADR (Architecture Decision Records): ключевые решения “почему так”

## Структура кода (ядро)
- `main.c` - старт системы, порядок инициализации, deep sleep логика (если включена)
- `j_wifi.c/.h` - Wi-Fi STA (сейчас как база радио-стека и источник канала; SSID может быть пустым для ESPNOW-only)
- `j_espnow_link.c/.h` + `j_espnow_proto.h` - транспорт управления и ACK
- `ctrl_bus.*` - состояние управления и очередь/применение команд
- `matrix_ws2812.*` - драйвер матрицы (буфер кадра, XY->index, show)
- `matrix_anim.*` - задача обновления кадров
- `fx_engine.*`, `fx_registry.*`, `fx_canvas.*` - движок эффектов и реестр
