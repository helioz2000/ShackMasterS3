idf_component_register(SRCS "rbg_led.c" "led_strip_encoder.c" "main.c" "wifi.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_rmt esp_driver_gpio
                    INCLUDE_DIRS ".")
