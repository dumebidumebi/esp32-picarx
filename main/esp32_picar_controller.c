// Gives us snprintf() for building the text shown on the LCD.
#include <stdio.h>

// Gives us strlen(), memset(), and memcpy() for working with text.
#include <string.h>

// Gives us ESP-IDF error codes and ESP_ERROR_CHECK().
#include "esp_err.h"

// Lets us print information and errors to the serial monitor.
#include "esp_log.h"

// Gives us common FreeRTOS types and timing tools.
#include "freertos/FreeRTOS.h"

// Lets FreeRTOS tasks safely pass data through a queue.
#include "freertos/queue.h"

// Lets us create tasks and pause them for a chosen amount of time.
#include "freertos/task.h"

// Our joystick module reads the two ADC axes and push button.
#include "joystick.h"

// Our LCD module controls the 16x2 display over I2C.
#include "lcd_i2c.h"

// This label appears beside this file's serial-monitor messages.
static const char *TAG = "controller";

// This handle lets both FreeRTOS tasks use the same queue.
// The queue carries joystick_reading_t structures between the tasks.
static QueueHandle_t joystick_queue;

// Turn the raw joystick numbers into a word a person can understand.
static const char *get_direction(const joystick_reading_t *reading)
{
    // This dead zone prevents small center changes from changing direction.
    // Check Y first for forward and backward movement.
    if (reading->raw_y > 2800) return "UP";
    if (reading->raw_y < 1300) return "DOWN";

    // Then check X for left and right movement.
    if (reading->raw_x < 1300) return "LEFT";
    if (reading->raw_x > 2800) return "RIGHT";

    // Values between the limits are treated as the center position.
    return "CENTER";
}

// Write exactly one clean 16-character row to the LCD.
static esp_err_t lcd_write_line(uint8_t row, const char *text)
{
    // We need 16 visible characters plus one '\0' string-ending byte.
    char line[17];

    // Find the number of characters supplied by the caller.
    size_t length = strlen(text);

    // Never copy more characters than the LCD row can display.
    if (length > 16) length = 16;

    // Pad every line with spaces to erase text left from the previous update.
    memset(line, ' ', 16);

    // Copy the caller's text over the spaces at the start of the row.
    memcpy(line, text, length);

    // Add the zero byte that ends a C string.
    line[16] = '\0';

    // Move the LCD cursor to column 0 of the selected row.
    esp_err_t result = lcd_i2c_set_cursor(0, row);
    if (result != ESP_OK) return result;

    // Send the completed 16-character line to the display.
    return lcd_i2c_write_text(line);
}

// This FreeRTOS task reads the joystick repeatedly.
static void joystick_task(void *argument)
{
    // This local structure holds one complete sample.
    joystick_reading_t reading;

    // A FreeRTOS task normally uses an endless loop to keep doing its job.
    while (1) {
        // Ask the joystick module to fill the reading structure.
        esp_err_t result = joystick_read(&reading);

        // Only use the reading when the hardware read succeeded.
        if (result == ESP_OK) {
            // This one-item queue always keeps the newest reading.
            // If old data is waiting, xQueueOverwrite replaces it.
            xQueueOverwrite(joystick_queue, &reading);

            // Also print the values so they can be checked in the monitor.
            ESP_LOGI(TAG, "X:%4d Y:%4d SW:%d", reading.raw_x,
                     reading.raw_y, reading.button_pressed);
        } else {
            // Print the error name when an ADC read fails.
            ESP_LOGE(TAG, "Joystick read failed: %s", esp_err_to_name(result));
        }

        // Sleep for 50 ms. This gives us about 20 joystick samples per second.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// This separate FreeRTOS task updates the LCD.
static void lcd_task(void *argument)
{
    // This receives one complete joystick sample from the queue.
    joystick_reading_t reading;

    // This holds one formatted LCD row plus its '\0' ending byte.
    char line[17];

    // Keep waiting for and displaying new joystick readings forever.
    while (1) {
        // Wait here until the joystick task places data in the queue.
        // portMAX_DELAY means there is no timeout while waiting.
        if (xQueueReceive(joystick_queue, &reading, portMAX_DELAY) == pdTRUE) {
            // Build the first row from the two raw ADC readings.
            snprintf(line, sizeof(line), "X:%4d Y:%4d", reading.raw_x,
                     reading.raw_y);

            // Display the raw readings on row 0, which is the top row.
            ESP_ERROR_CHECK(lcd_write_line(0, line));

            // Build the second row from the direction and button state.
            // %-10s gives the direction a fixed-width space on the screen.
            snprintf(line, sizeof(line), "%-10s SW:%d",
                     get_direction(&reading), reading.button_pressed);

            // Display the direction and switch state on the bottom row.
            ESP_ERROR_CHECK(lcd_write_line(1, line));

            // Update the LCD five times per second.
            // The joystick task can continue sampling while this task waits.
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

// ESP-IDF starts our application here after the ESP32 boots.
void app_main(void)
{
    // Set up the ADC channels and joystick button.
    // ESP_ERROR_CHECK stops the program and prints the error if setup fails.
    ESP_ERROR_CHECK(joystick_init());

    // Set up the I2C bus and initialize the LCD.
    ESP_ERROR_CHECK(lcd_i2c_init());

    // Start with an empty display.
    ESP_ERROR_CHECK(lcd_i2c_clear());

    // Create a queue that holds exactly one joystick_reading_t structure.
    joystick_queue = xQueueCreate(1, sizeof(joystick_reading_t));

    // A NULL handle means FreeRTOS could not allocate memory for the queue.
    if (joystick_queue == NULL) {
        ESP_LOGE(TAG, "Could not create joystick queue");
        return;
    }

    // Start the joystick task with a 3072-byte stack and priority 5.
    // Its higher priority lets input sampling run before the display task.
    xTaskCreate(joystick_task, "joystick_task", 3072, NULL, 5, NULL);

    // Start the LCD task with a 3072-byte stack and priority 4.
    xTaskCreate(lcd_task, "lcd_task", 3072, NULL, 4, NULL);
}
