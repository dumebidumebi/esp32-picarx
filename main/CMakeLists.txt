# Tell ESP-IDF which C files belong to our main program.
idf_component_register(
    SRCS "esp32_picar_controller.c" "joystick.c" "lcd_i2c.c"

    # Let these source files include header files from this folder.
    INCLUDE_DIRS "."

    # Link the ESP-IDF ADC, GPIO, and I2C drivers used by our code.
    REQUIRES esp_adc esp_driver_gpio esp_driver_i2c
)
