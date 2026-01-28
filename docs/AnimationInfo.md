# Анимации (FX) — правила и текущий набор

## Инвариант по файлам эффектов
- **Сложные** эффекты (пример: FIRE) живут в отдельном именованном файле: `main/fx_effects_<name>.c`
- **Простые** эффекты живут пачками (до ~10) в `main/fx_effects_simple.c`

## Схема ID
- Простые: `0xEA01..0xEAxx`
- Сложные: `0xCA01..0xCAxx`
- Debug/Service: `0xED01..` (не для “обычного” пользователя)

`effect_id` — `uint16`, используется в ctrl_bus и ESPNOW.

## Текущий активный набор
Простые (`main/fx_effects_simple.c`):
- `0xEA01` SNOW FALL
- `0xEA02` CONFETTI
- `0xEA03` DIAG RAINBOW
- `0xEA04` GLITTER RAINBOW
- `0xEA05` RADIAL RIPPLE
- `0xEA06` CUBES
- `0xEA07` ORBIT DOTS

Сложные:
- `0xCA01` FIRE (`main/fx_effects_fire.c`)

Debug:
- `0xED01` DOA DEBUG (появляется в списке только при включённом DOA debug)
