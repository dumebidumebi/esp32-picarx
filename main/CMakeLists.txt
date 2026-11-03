# Tell ESP-IDF which C files belong to our main program.
idf_component_register(
    SRCS "esp32_picar_controller.c" "joystick.c" "lcd_i2c.c" "wifi_udp.c"

    # Let these source files include header files from this folder.
    INCLUDE_DIRS "."

    # Link the ESP-IDF hardware, Wi-Fi, network, and storage components we use.
    REQUIRES esp_adc esp_driver_gpio esp_driver_i2c esp_wifi esp_event esp_netif esp_timer nvs_flash lwip
)
