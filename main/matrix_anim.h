#pragma once

/*
 * matrix_anim.h
 *
 * Назначение:
 *   Высокоуровневый модуль тестовых анимаций для матрицы WS2812.
 *   Отвечает за:
 *     - запуск отдельной FreeRTOS-задачи, которая генерирует кадры,
 *     - вызов matrix_ws2812_show() с заданной частотой обновления,
 *     - установку тестовой яркости (в проекте сейчас 40%).
 *
 * Инварианты:
 *   - Эта задача предполагает, что она "владеет" матрицей:
 *       не вызывай параллельно другие show()/refresh из других задач.
 *   - matrix_ws2812_init() вызывается внутри matrix_anim_start().
 */

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

// Запуск task анимации + инициализация WS2812 на указанном GPIO.
esp_err_t matrix_anim_start(gpio_num_t data_gpio);

// Останов (мягкий): выставляет флаг выхода, задача завершится сама.
void      matrix_anim_stop(void);

// Пауза/возобновление (держит последний кадр на матрице).
void matrix_anim_pause_toggle(void);

// Переключение анимаций (минимум 2 штуки реализованы в matrix_anim.c).
void matrix_anim_next(void);
void matrix_anim_prev(void);

