# HANDOFF — Jinny Lamp (ESP32-S3 + XVF3800 + WS2812)


Связанные документы:
- docs/README.md — входная дверь в проект, что это и куда смотреть
- docs/architecture.md — модули, потоки, задачи, инварианты
- docs/commands.md — команды разработчика + семантика управляющих команд
- docs/espnow.md — ESP-NOW: канал, pairing, протокол CTRL/ACK
- docs/adr/ADR-0001*.md — ключевое решение по связи (ESP-NOW + Wi-Fi STA)
- docs/fx_fire.md — детальная документация сложного эффекта FIRE
- docs/analysis/tech_debt.md — риски и задачи по доводке (P0–P2)


## 2) Аппаратная архитектура (факт)

### 2.1 MCU и плата
- MCU: ESP32-S3 (XIAO), интегрирован на плате ReSpeaker XVF3800 (voice DSP).

### 2.2 Связь ESP32-S3 <-> XVF3800
- I2C: управление XVF и его GPIO, управление GPO.
- I2S: аудио RX/TX (см. audio_i2s.*).

### 2.3 Световая часть
- WS2812B: 3 матрицы 16×16, daisy-chain, итого 48×16 = 768 LED.
- DATA: GPIO3 ESP32-S3 -> 74AHCT125 (5V) -> серийный резистор 330–470 Ω -> матрицы.
- Питание матриц: +5V напрямую, GND коммутируется low-side N-MOSFET.
- Gate MOSFET: XVF3800 GPO X0D11, Rgate ~100 Ω, pulldown ~100 kΩ (опц. Cgs 10–47 nF).

Критические инварианты питания WS2812:
- Включение: DATA=LOW -> MOSFET ON -> задержка -> передача данных.
- Выключение: stop anim -> DATA=LOW -> MOSFET OFF.

---

## 3) Программная архитектура (ESP-IDF)

### 3.1 База
- ESP-IDF: v5.5.1
- Код приложения: main/
- WS2812: led_strip (RMT + DMA на ESP32-S3).

### 3.2 Важные модули (куда смотреть)
- main.c — app_main(), порядок инициализации, (возможная) sleep логика.
- matrix_ws2812.* — драйвер матрицы “framebuffer + show”.
- matrix_anim.* — задача рендера кадров (единственный владелец show).
- fx_engine.* + fx_canvas.* + fx_registry.* — движок эффектов и реестр.
- ctrl_bus.* — источник истины состояния: effect_id / brightness / speed_pct / paused / power + state_seq.
- j_wifi.* — Wi-Fi STA, источник текущего канала (AP channel или fallback).
- j_espnow_link.* + j_espnow_proto.h — прием команд, применение через ctrl_bus, отправка ACK snapshot.
- input_ttp223.* — локальные клики/лонги (если включено).
- audio_i2s.* / asr_debug.* — аудио тракт и диагностика (asr_debug может “шуметь” логами).

---

## 4) Wireless: что принято и как работает (факт)

Решение зафиксировано ADR: транспорт управления ESP-NOW, Wi-Fi STA как база и будущий MQTT. Источник истины состояния — лампа, пульт — клиент с кэшем и подтверждением по ACK. Канал ESPNOW следует каналу Wi-Fi STA, при пустом SSID используется fallback channel. См. docs/adr и docs/espnow.md.

Факты текущей связки (MAC/Node ID) см. docs/espnow.md.

---

## 5) Управление состоянием и протокол (факт)

Источник истины: ctrl_bus на лампе.
Состояние (ctrl_state_t):
- effect_id (uint16)
- brightness (uint8, 0..255)
- speed_pct (uint16, 10..300)
- paused (bool)
- power (bool)
- state_seq (uint32, монотонно растёт при применении)

ESP-NOW команды:
- POWER (0/1)
- SET_ANIM (effect_id)
- SET_PAUSE (0/1)
- SET_BRIGHT (0..255)
- SET_SPEED_PCT (10..300)

ACK от лампы возвращает snapshot состояния: effect_id/brightness/paused/speed_pct/state_seq.

Семантика и диапазоны см. docs/commands.md и docs/espnow.md.

---

## 6) Анимации и структура эффектов (ОБНОВЛЕНО сегодня)

### 6.1 Правило структуры файлов эффектов (инвариант проекта)
- “Сложные” эффекты (пример: FIRE) всегда живут в отдельном именованном файле.
  Пример: main/fx_effects_fire.c (док: docs/fx_fire.md).
- “Простые” эффекты живут пачками примерно по 10 штук в файле.
  Текущий файл простых эффектов: main/fx_effects_simple.c.

Цель правила:
- не тащить большие сложные эффекты в общий файл
- проще чистить/добавлять простые эффекты партиями без мусора и конфликтов символов

### 6.2 Текущее активное множество эффектов (8 шт)
Сейчас в реестре оставлено 8 эффектов, остальные старые эффекты вычищены из реестра и из сборки.

Простые (в main/fx_effects_simple.c):
- 0xEA01 SNOW FALL           -> fx_snow_fall_render
- 0xEA02 CONFETTI            -> fx_confetti_render
- 0xEA03 DIAG RAINBOW         -> fx_diag_rainbow_render
- 0xEA04 GLITTER RAINBOW      -> fx_glitter_rainbow_render
- 0xEA05 RADIAL RIPPLE        -> fx_radial_ripple_render
- 0xEA06 CUBES                -> fx_cubes_render
- 0xEA07 ORBIT DOTS           -> fx_orbit_dots_render

Сложные:
- 0xCA01 FIRE                 -> fx_fire_render (main/fx_effects_fire.c)

Примечание:
- Ранее была проблема линковки `fx_orbit_dots_render` из-за исключения файла из SRCS в main/CMakeLists.txt.
  Исправлено: файл, содержащий символ, должен быть включён в SRCS, иначе получите undefined reference на этапе линковки.

### 6.3 Идентификаторы эффектов (ID схема)
Новая схема:
- простые: 0xEA01..0xEAxx
- сложные: 0xCA01..0xCAxx

Важно:
- effect_id хранится и передаётся как uint16.
- дефолтный эффект должен быть валидным effect_id из fx_registry.

---

## 7) Инварианты (НЕ ЛОМАТЬ)

- Один владелец show: matrix_ws2812_show вызывается только из matrix_anim task.
- ESPNOW зависит от Wi-Fi канала: каналом управляет j_wifi, ESPNOW следует ему.
- Источник истины состояния — лампа (ctrl_bus), пульт только отправляет команды и принимает ACK snapshot.
- Power sequencing WS2812: DATA=LOW при включении и выключении питания матриц.
- Яркость — через софтверное scaling (как реализовано сейчас в драйвере/движке).

---

## 8) Текущее состояние (факт на конец сессии)

- Сборка: OK
- Прошивка и проверка на железе: OK
- ESP-NOW связь с пультом: OK (команды проходят, ACK snapshot возвращается)
- 8 эффектов работают штатно
- Реестр и набор эффектов очищены: мусор от старых пакетов убран

Ограничение (прежнее, актуально):
- SET_ANIM корректен только если таблица effect_id на пульте совпадает с fx_registry лампы
  (или будет реализован протокол запроса списка эффектов).

---

## 9) Known issues / Tech debt (куда смотреть)
Список рисков и задач: docs/analysis/tech_debt.md
Особо важное:
- matrix_anim_stop без гарантированного “join” перед выключением питания матриц
- семантика PAUSE (нужно “заморозить”, а не “перезапускать”)
- Wi-Fi канал при будущем MQTT
- размер factory partition и рост прошивки

---

## 10) Как быстро продолжить работу в следующей сессии (что прислать)

Чтобы мгновенно войти в контекст без пересылки всего репо, достаточно:
1) вывод:
   - `git describe --tags --always --dirty`
   - `git log -15 --oneline`
2) файлы/фрагменты (если менялись):
   - main/fx_registry.c (s_fx[] и функции registry)
   - main/fx_effects_simple.c (простые эффекты)
   - main/fx_effects_fire.c (если трогали FIRE)
   - main/CMakeLists.txt (SRCS)
   - main/ctrl_bus.c (структура состояния и дефолт effect_id)
3) если проблема по линковке: строка ошибки линкера + `Select-String build/jinny_lamp_brain.map -Pattern "<symbol>"`

---

## 11) Команды разработчика (быстро)
См. docs/commands.md, кратко:
- idf.py build
- idf.py -p COMx flash
- idf.py -p COMx monitor
- idf.py size / idf.py size-components (контроль размера)
