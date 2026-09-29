#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t lcd_i2c_init(void);
esp_err_t lcd_i2c_clear(void);
esp_err_t lcd_i2c_set_cursor(uint8_t column, uint8_t row);
esp_err_t lcd_i2c_write_text(const char *text);
