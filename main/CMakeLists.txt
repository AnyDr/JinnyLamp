idf_component_register(
    SRCS
        "main.c"
        "audio_i2s.c"
        "asr_debug.c"
        "led_control.c"
        "matrix_ws2812.c"
        "matrix_anim.c"
        "input_ttp223.c"
        "sense_acs758.c"
        "xvf_i2c.c"
        "ctrl_bus.c"
        "fx_engine.c"
        "fx_registry.c"
        "fx_canvas.c"
        "fx_effects_ported_1.c"
        "fx_effects_noise_1.c"
        "fx_effects_noise_2.c"
        "fx_effects_noise_3.c"
        "fx_effects_classic_4.c"
        "fx_effects_geo_5.c"
        "fx_effects_geo_6.c"
        "fx_effects_geo_7.c"
        "fx_effects_geo_8.c"
        "fx_effects_geo_9.c"
        "fx_effects_geo_10.c"
        "fx_effects_geo_11.c"
        "fx_effects_pack12_basic.c"
        "fx_effects_geo_12.c"
        "fx_effects_geo_14.c"



    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_adc
        esp_timer
        driver
        led_strip
         
         
        
)
