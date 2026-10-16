idf_component_register(SRCS "main.c" "ble.c" "adv_parser.c" "display.c" INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_driver_uart esp_driver_spi esp_driver_gpio esp_lcd json esp_timer)
