#pragma once

// Gives us fixed-size number types such as uint8_t.
#include <stdint.h>

// Gives us ESP-IDF error codes such as ESP_OK.
#include "esp_err.h"

// Set up the I2C bus and prepare the LCD for use.
esp_err_t lcd_i2c_init(void);

// Remove all characters from the LCD.
esp_err_t lcd_i2c_clear(void);

// Move the LCD cursor to a column and row.
esp_err_t lcd_i2c_set_cursor(uint8_t column, uint8_t row);

// Write a normal C string at the current cursor position.
esp_err_t lcd_i2c_write_text(const char *text);
