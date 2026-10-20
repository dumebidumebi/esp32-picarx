#pragma once

// Gives us the bool type for true and false values.
#include <stdbool.h>

// Gives us ESP-IDF error codes such as ESP_OK.
#include "esp_err.h"

// This structure groups one complete joystick reading together.
typedef struct {
    // The raw left/right ADC value. It is normally between 0 and 4095.
    int raw_x;

    // The raw up/down ADC value. It is normally between 0 and 4095.
    int raw_y;

    // This is true while the joystick button is pressed.
    bool button_pressed;
} joystick_reading_t;

// Set up the ADC channels and the joystick button pin.
esp_err_t joystick_init(void);

// Read the joystick and place the result inside the structure we pass in.
esp_err_t joystick_read(joystick_reading_t *reading);
