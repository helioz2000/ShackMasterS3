idf_component_register(SRCS "usbhost.c" "rbg_led.c" "led_strip_encoder.c" "main.c" "wifi.c" "httpserver.c" "usbhost.c"
                    PRIV_REQUIRES usb esp_wifi esp_http_server nvs_flash esp_driver_rmt esp_driver_gpio
                    EMBED_TXTFILES "setup.html" "index.html"
                    INCLUDE_DIRS ".")
