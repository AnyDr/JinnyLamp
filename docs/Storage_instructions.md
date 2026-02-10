
* проект: `D:\esp\jinny_lamp_brain`
* исходники SPIFFS лежат в:
  `D:\esp\jinny_lamp_brain\spiffs_storage\voice\boot_greeting.pcm`
* partition `storage`:

  * offset: `0x560000`
  * size: `0x2A0000` = **2 752 512 bytes**
* COM-порт платы: **COM12**

Ниже инструкция **под твою реальную структуру**, плюс как это масштабировать под десятки файлов.

---

# ЧАСТЬ 1. Разовая правильная схема (база)

## 1. Каноническая структура в проекте (рекомендую зафиксировать)

Оставляем именно так и больше не меняем:

```
D:\esp\jinny_lamp_brain\
├── main\
├── components\
├── partitions.csv
├── spiffs_storage\          ← ИСТОЧНИК файловой системы
│   └── voice\
│       ├── boot_greeting.pcm
│       ├── listen_start.pcm
│       ├── thinking_1.pcm
│       ├── thinking_2.pcm
│       └── ...
└── spiffs_storage.bin       ← СГЕНЕРИРОВАННЫЙ ОБРАЗ
```

Правило:

* **`spiffs_storage/` — это всегда то, что будет `/spiffs` на устройстве**
* ничего из этого **не компилируется**, это просто данные

---

## 2. Проверка файлов перед сборкой

Перед каждым созданием образа:

```powershell
cd D:\esp\jinny_lamp_brain
dir .\spiffs_storage\voice
```

Ты должен видеть:

```
boot_greeting.pcm
```

(и позже другие файлы)

---

## 3. Генерация SPIFFS-образа (под твою разметку)

Команда **строго такая**:

```powershell
cd D:\esp\jinny_lamp_brain

python $env:IDF_PATH\components\spiffs\spiffsgen.py `
  2752512 `
  .\spiffs_storage `
  spiffs_storage.bin
```

Что здесь важно:

* `2752512` — **обязан** совпадать с размером `storage`
* `.\\spiffs_storage` — **корень файловой системы**
* результат: `spiffs_storage.bin` в корне проекта

---

## 4. Прошивка SPIFFS в плату (COM12)

Твоя команда прошивки:

```powershell
python -m esptool --chip esp32s3 --port COM12 --baud 921600 write_flash 0x560000 spiffs_storage.bin
```

Где:

* `0x560000` — offset `storage`
* `COM12` — твоя плата

⚠️ **Это не OTA**, это прямая запись data-partition, и это нормально.

---

## 5. Проверка на плате

После перезагрузки в логах ты уже выводишь:

```
STORAGE_FS: List '/spiffs'
```

Ожидаемо увидеть:

```
/spiffs/voice/boot_greeting.pcm
```

Если есть — всё, база готова.

---

# ЧАСТЬ 2. Как жить дальше, когда файлов станет 10–30+

## 1. Соглашение по неймингу (очень важно)

Сразу фиксируем правило, чтобы код был чистым:

```
event_<событие>[_<вариант>].pcm
```

Примеры:

```
boot_greeting.pcm
listen_start.pcm
listen_stop.pcm

thinking_1.pcm
thinking_2.pcm
thinking_3.pcm

cmd_ok_1.pcm
cmd_ok_2.pcm

cmd_fail_1.pcm
cmd_fail_2.pcm
```

Это **идеально ложится** на твою будущую логику:

* событие → список файлов → случайный выбор без повторов

---

## 2. Добавление нового файла (рабочий цикл)

Каждый раз одно и то же:

1️⃣ Кладёшь файл в:

```
D:\esp\jinny_lamp_brain\spiffs_storage\voice\
```

2️⃣ Генерируешь образ:

```powershell
python $env:IDF_PATH\components\spiffs\spiffsgen.py `
  2752512 `
  .\spiffs_storage `
  spiffs_storage.bin
```

3️⃣ Шьёшь:

```powershell
python -m esptool --chip esp32s3 --port COM12 --baud 921600 write_flash 0x560000 spiffs_storage.bin
```

4️⃣ Ребут → готово.

# Jinny Lamp — Storage / Flashing Instructions (models + SPIFFS)

## 1) Partition layout (current)

- `model`   (data, subtype 64)  size **2560K**  offset **0x320000**
- `storage` (data, spiffs)      size **2432K**  offset **0x5A0000**

OTA app slots:
- `ota_0` 1536K
- `ota_1` 1536K

This layout is chosen to support ESP-SR models (MultiNet) in `model` while keeping voice pack in SPIFFS.

---

## 2) SPIFFS content (voice pack v2)

Directory layout:
`spiffs_storage/v/{lc,ss,cmd,srv,ota,err}/`

Target mount path in runtime:
`/spiffs/v/...`

File naming:
`<group>-<event_id>-<variant>.wav`

Audio format:
- WAV IMA ADPCM 4-bit, mono, 16000 Hz

---

## 3) Build SPIFFS image (storage)

SPIFFS size in bytes:
- 2432K = 2432 * 1024 = **2490368** bytes (0x260000)

Command (PowerShell):

```powershell
cd D:\esp\jinny_lamp_brain

python $env:IDF_PATH\components\spiffs\spiffsgen.py `
  2490368 `
  .\spiffs_storage `
  spiffs_storage.bin
