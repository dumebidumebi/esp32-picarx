#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "joystick.h"
#include "lcd_i2c.h"

static const char *TAG = "controller";
static QueueHandle_t joystick_queue;

static const char *get_direction(const joystick_reading_t *reading)
{
    // This dead zone prevents small center changes from changing direction.
    if (reading->raw_y > 2800) return "UP";
    if (reading->raw_y < 1300) return "DOWN";
    if (reading->raw_x < 1300) return "LEFT";
    if (reading->raw_x > 2800) return "RIGHT";
    return "CENTER";
}

static esp_err_t lcd_write_line(uint8_t row, const char *text)
{
    char line[17];
    size_t length = strlen(text);
    if (length > 16) length = 16;

    // Pad every line with spaces to erase text left from the previous update.
    memset(line, ' ', 16);
    memcpy(line, text, length);
    line[16] = '\0';

    esp_err_t result = lcd_i2c_set_cursor(0, row);
    if (result != ESP_OK) return result;
    return lcd_i2c_write_text(line);
}

static void joystick_task(void *argument)
{
    joystick_reading_t reading;

    while (1) {
        esp_err_t result = joystick_read(&reading);
        if (result == ESP_OK) {
            // This one-item queue always keeps the newest reading.
            xQueueOverwrite(joystick_queue, &reading);
            ESP_LOGI(TAG, "X:%4d Y:%4d SW:%d", reading.raw_x,
                     reading.raw_y, reading.button_pressed);
        } else {
            ESP_LOGE(TAG, "Joystick read failed: %s", esp_err_to_name(result));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void lcd_task(void *argument)
{
    joystick_reading_t reading;
    char line[17];

    while (1) {
        if (xQueueReceive(joystick_queue, &reading, portMAX_DELAY) == pdTRUE) {
            snprintf(line, sizeof(line), "X:%4d Y:%4d", reading.raw_x,
                     reading.raw_y);
            ESP_ERROR_CHECK(lcd_write_line(0, line));

            snprintf(line, sizeof(line), "%-10s SW:%d",
                     get_direction(&reading), reading.button_pressed);
            ESP_ERROR_CHECK(lcd_write_line(1, line));

            // Update the LCD five times per second.
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(joystick_init());
    ESP_ERROR_CHECK(lcd_i2c_init());
    ESP_ERROR_CHECK(lcd_i2c_clear());

    joystick_queue = xQueueCreate(1, sizeof(joystick_reading_t));
    if (joystick_queue == NULL) {
        ESP_LOGE(TAG, "Could not create joystick queue");
        return;
    }

    xTaskCreate(joystick_task, "joystick_task", 3072, NULL, 5, NULL);
    xTaskCreate(lcd_task, "lcd_task", 3072, NULL, 4, NULL);
}
