#pragma once

/*
 * audio_tone_test.h
 *
 * Временный тон-тест I2S TX (ESP -> XVF).
 * Задумка:
 *   - включается compile-time define в .c
 *   - стартует один раз на boot по явному вызову
 *   - легко удаляется: убрать include+вызов или выключить define
 */

#ifdef __cplusplus
extern "C" {
#endif

void audio_tone_test_start(void);

#ifdef __cplusplus
}
#endif
