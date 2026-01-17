Команды разработчика: build/flash/monitor, полезные findstr/grep, генерация snapshot/tree, теги/релизы.

# Команды разработчика (Jinny Lamp)

## Build / Flash / Monitor

Обычный цикл:

cd D:\esp\jinny_lamp_brain
idf.py build

Полная очистка:
idf.py fullclean
idf.py build

Прошивка:
idf.py -p COM12 flash
idf.py -p COM12 monitor

Меню конфигурации:
idf.py -p COM12 menuconfig

## Git checkpoint (практика сессий)
Рекомендуемый порядок в конце сессии:

Отправка всех изменений проекта на текущую ветку на гитхабе:

cd D:\esp\jinny_lamp_brain
git status
git add -A
git commit -m "OTA_finalised"
git push origin


git tag -a (backup/<on\off> -m "<msg>") - ??    
git push origin (backup/<name>)  - ??


git push origin stable/work     переключение на ветку стейбл
git branch --show-current       покажет ветку, статус файлов и т.д.
git status
git log --oneline -n 1

## Снимок репозитория (git snapshot)
Рекомендуется обновлять `docs/git_snapshot.txt` при крупных изменениях:
- `git describe --tags --always --dirty`
- `git log -20 --oneline`
- ключевые CONFIG (ESPNOW/WIFI/NODE_ID)

## tree (структура файлов) без проблем с кодировкой в Windows
PowerShell:
`cmd /c "chcp 65001>nul & tree /F /A" | Out-File -Encoding utf8 docs\tree.txt`

## Partition table
Фактическая таблица разделов берётся из `build/partition_table/partition-table.bin`.

Печать в текст:
`python $env:IDF_PATH\components\partition_table\gen_esp32part.py build\partition_table\partition-table.bin > docs/analysis/partition_table.txt`

## Отключение шумных логов (пример)
Если ASR debug начинает мешать:
- `esp_log_level_set("ASR_DEBUG", ESP_LOG_WARN);`
Добавлять только по необходимости, чтобы не ломать отладку.

# Команды управления Jinny Lamp

## Источник истины
Состояние хранится и применяется в лампе (ctrl_bus), пульт только отправляет команды.
Лампа подтверждает применённое состояние через ESPNOW ACK.

## Параметры состояния (ctrl_state_t)
- effect_id (uint16)
- brightness (uint8): диапазон 1..255 (0 допустим для “выкл”, но нужно оговаривать семантику)
- speed_pct (uint16): 10..300 (%)
- paused (bool)
- seq (uint32): увеличивается на каждое применённое изменение

## Команды ESPNOW (j_esn_cmd_t)
1) POWER
   - value_u16: 0 = off, 1 = on
   - Семантика: “off” останавливает вывод и выключает силовую часть матриц (через MOSFET), “on” включает и возобновляет.

2) SET_ANIM
   - value_u16: effect_id
   - Требование: effect_id должен существовать в fx_registry лампы.

3) SET_PAUSE
   - value_u16: 0/1
   - Семантика: пауза останавливает фазу/рендер (или замораживает картинку), но питание может оставаться включённым.

4) SET_BRIGHT
   - value_u16: 0..255
   - На уровне драйвера яркость масштабируется софтверно.

5) SET_SPEED_PCT
   - value_u16: 10..300
   - Применяется как множитель к base_step эффекта.

## ACK (ответ лампы)
В ACK всегда возвращается снимок:
- effect_id / brightness / paused / speed_pct / state_seq
