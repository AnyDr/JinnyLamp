# DOA (Direction of Arrival) — Jinny Lamp

## 1) Назначение
DOA — угол направления источника речи (0..360°), читается из XVF3800 и доступен другим компонентам.
DOA debug — это только UI/лог для проверки.

## 2) Текущее решение (реализовано)
### 2.1 Источник DOA
Команда XVF: `AEC_AZIMUTH_VALUES`
- payload: 4×float32 little-endian, units=radians
- индексы: beam1, beam2, free-running, auto-selected
- используем: `[3] auto-selected`

I2C mapping проекта:
- resid=33, cmd=75, len=16 (read cmd = 0x80|cmd)

### 2.2 Частота / валидность
- update: 10 Hz
- если данные невалидны: snapshot не обновляем (потребитель может делать hold+fade)

### 2.3 Калибровка угла под матрицу
- `offset_deg` + `inv` + wrap 0..360
- соглашение: LED0 = 0°/360°, рост по часовой (как принято в проекте)

## 3) Debug-режим (опционально)
FX `DOA DEBUG`:
- 1 пиксель: X=угол (0..15), Y=уровень (0..47)
- ниже порога: пиксель не горит

Debug влияет только на:
- наличие DOA DEBUG в FX list (пульт подтягивает список от лампы)
- лог DOA в monitor

Сервис DOA остаётся доступным всегда.

## 4) План улучшения (не блокер)
- Перейти на `AUDIO_MGR_SELECTED_AZIMUTHS` (processed azimuth, NaN=нет речи) как более “UI-правильный” DOA
- Уровень брать из `AEC_SPENERGY_VALUES` + envelope (attack/release)


# [DOA PASSPORT] AEC_AZIMUTH_VALUES (XVF3800)

Источник: XVF3800, команда AEC_AZIMUTH_VALUES (AEC module)

Формат:
- payload: 4 × float32 IEEE754, little-endian
- units: radians

Индексы:
- [0] Focused beam 1 azimuth (rad)
- [1] Focused beam 2 azimuth (rad)
- [2] Free-running beam azimuth (rad)
- [3] Auto-selected beam azimuth (rad)  ← используем в проекте

Сопутствующий канал уровня речи:
- AEC_SPENERGY_VALUES: 4 × float32 (map 1:1 к индексам выше)

I2C mapping проекта (Jinny Lamp):
- resid = 33
- cmd  = 75
- len  = 16
- read cmd = 0x80 | cmd

Policy проекта:
- DOA snapshot обновляется 10 Hz
- при невалидных данных: hold last (и визуальный fade делает потребитель)

Статус:
- AEC_AZIMUTH_VALUES — реализовано и подтверждено.

TODO (улучшение):
- найти/зафиксировать mapping для AUDIO_MGR_SELECTED_AZIMUTHS (2×rad, processed + auto), чтобы получить “NaN=нет речи”.
