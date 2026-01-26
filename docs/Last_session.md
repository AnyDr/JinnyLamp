Состояние на конец сессии (Lamp-side OTA)

Реализован OTA вход по ESPNOW: команда J_ESN_CMD_OTA_START.

При команде лампа:

поднимает SoftAP + HTTP OTA портал (SSID JINNY-OTA, pass jinny12345, port 80, ttl/timeout 300s),

отправляет на пульт HELLO extension J_ESN_HELLO_OTA_INFO_RSP (send-only) с полями ota_status + ttl_s + ssid[33] + pass[64],

делает 3× resend для надёжности,

шлёт ACK на команду.

Адрес OTA после подключения к AP: http://192.168.4.1/update (gateway AP по умолчанию 192.168.4.1).

Исправлена совместимость протокола с пультом: убрали port из OTA-info, поле статуса унифицировано как ota_status, размеры ssid/pass совпадают.

Что осталось “на потом” (ближайшее)

Проверить unintended anti-rollback в bootloader config (проверить sdkconfig на ANTI_ROLLBACK/ROLLBACK и снять галку в menuconfig).

