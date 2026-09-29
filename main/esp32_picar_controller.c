#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_i2c.h"

static const char *TAG = "lcd_test";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting LCD test");

    esp_err_t result = lcd_i2c_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LCD setup failed: %s", esp_err_to_name(result));
        ESP_LOGE(TAG, "Check GND, 5V, SDA GPIO21 and SCL GPIO22");
        return;
    }

    ESP_ERROR_CHECK(lcd_i2c_clear());
    ESP_ERROR_CHECK(lcd_i2c_set_cursor(0, 0));
    ESP_ERROR_CHECK(lcd_i2c_write_text("LINE 1: HELLO"));
    ESP_ERROR_CHECK(lcd_i2c_set_cursor(0, 1));
    ESP_ERROR_CHECK(lcd_i2c_write_text("LINE 2: 12345"));

    ESP_LOGI(TAG, "LCD test text sent successfully");

    // Keep both lines visible until the ESP32 is reset or unplugged.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
