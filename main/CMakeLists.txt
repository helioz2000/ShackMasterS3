idf_component_register(SRCS "rbg_led.c" "led_strip_encoder.c" "main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_rmt
                    INCLUDE_DIRS ".")
