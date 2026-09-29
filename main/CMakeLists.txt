idf_component_register(
    SRCS "esp32_picar_controller.c" "joystick.c" "lcd_i2c.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc esp_driver_gpio esp_driver_i2c
)
