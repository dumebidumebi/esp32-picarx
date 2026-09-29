#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    int raw_x;
    int raw_y;
    bool button_pressed;
} joystick_reading_t;

esp_err_t joystick_init(void);
esp_err_t joystick_read(joystick_reading_t *reading);
