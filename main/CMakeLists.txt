idf_component_register(
    SRCS "esp32_picar_controller.c" "lcd_i2c.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c
)
