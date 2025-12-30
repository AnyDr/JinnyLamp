idf_component_register(
    SRCS
        "main.c"
        "audio_i2s.c"
        "asr_debug.c"
        "led_control.c"
        "matrix_ws2812.c"
        "matrix_anim.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        driver
        led_strip
)
