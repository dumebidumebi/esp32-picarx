#include "lcd_i2c.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_I2C_PORT       I2C_NUM_0
#define LCD_SDA_GPIO       21
#define LCD_SCL_GPIO       22
#define LCD_I2C_FREQUENCY  100000
#define LCD_TIMEOUT_MS     100

// Common PCF8574 backpack wiring:
// P0=RS, P1=RW, P2=Enable, P3=Backlight, P4-P7=LCD data D4-D7.
#define LCD_RS        0x01
#define LCD_ENABLE    0x04
#define LCD_BACKLIGHT 0x08

static const char *TAG = "lcd_i2c";
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t lcd_handle;
static bool initialized;
static uint8_t backlight_value;
static uint8_t detected_address;

static esp_err_t expander_write(uint8_t value)
{
    value |= backlight_value;
    return i2c_master_transmit(lcd_handle, &value, 1, LCD_TIMEOUT_MS);
}

static esp_err_t pulse_enable(uint8_t value)
{
    // Match Freenove's driver: put the nibble on D4-D7 while Enable is high,
    // then lower Enable so the LCD saves that nibble.
    esp_err_t result = expander_write(value | LCD_ENABLE);
    if (result != ESP_OK) return result;
    esp_rom_delay_us(10);
    result = expander_write(value & (uint8_t)~LCD_ENABLE);
    esp_rom_delay_us(100);
    return result;
}

static esp_err_t write_nibble(uint8_t nibble, uint8_t mode)
{
    uint8_t value = (uint8_t)((nibble & 0x0F) << 4) | mode;
    return pulse_enable(value);
}

static esp_err_t write_byte(uint8_t value, uint8_t mode)
{
    esp_err_t result = write_nibble(value >> 4, mode);
    if (result != ESP_OK) return result;
    return write_nibble(value, mode);
}

static esp_err_t write_command(uint8_t command)
{
    esp_err_t result = write_byte(command, 0);
    if (command == 0x01 || command == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return result;
}

esp_err_t lcd_i2c_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = LCD_I2C_PORT,
        .sda_io_num = LCD_SDA_GPIO,
        .scl_io_num = LCD_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t result = i2c_new_master_bus(&bus_config, &bus_handle);
    if (result != ESP_OK) return result;

    // Freenove uses 0x27 for PCF8574T and 0x3F for PCF8574AT.
    detected_address = 0x27;
    result = i2c_master_probe(bus_handle, detected_address, LCD_TIMEOUT_MS);
    if (result != ESP_OK) {
        detected_address = 0x3F;
        result = i2c_master_probe(bus_handle, detected_address, LCD_TIMEOUT_MS);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No LCD answered at 0x27 or 0x3F");
        return result;
    }
    ESP_LOGI(TAG, "LCD backpack found at I2C address 0x%02X", detected_address);

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = detected_address,
        .scl_speed_hz = LCD_I2C_FREQUENCY,
    };
    result = i2c_master_bus_add_device(bus_handle, &device_config, &lcd_handle);
    if (result != ESP_OK) return result;

    // Follow Freenove's bundled LCD initialization sequence.
    vTaskDelay(pdMS_TO_TICKS(50));
    backlight_value = 0;
    result = expander_write(0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(20));

    // HD44780 power-up sequence: enter 4-bit mode.
    result = write_nibble(0x03, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(5));
    result = write_nibble(0x03, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(1));
    result = write_nibble(0x03, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(1));
    result = write_nibble(0x02, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(1));

    // Two lines/5x8, display on/cursor off, clear, increment, and home.
    if ((result = write_command(0x28)) != ESP_OK) return result;
    if ((result = write_command(0x0C)) != ESP_OK) return result;
    if ((result = write_command(0x01)) != ESP_OK) return result;
    if ((result = write_command(0x06)) != ESP_OK) return result;
    if ((result = write_command(0x02)) != ESP_OK) return result;

    backlight_value = LCD_BACKLIGHT;
    if ((result = expander_write(0)) != ESP_OK) return result;

    initialized = true;
    return ESP_OK;
}

esp_err_t lcd_i2c_clear(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    return write_command(0x01);
}

esp_err_t lcd_i2c_set_cursor(uint8_t column, uint8_t row)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (column >= 16 || row >= 2) return ESP_ERR_INVALID_ARG;
    static const uint8_t row_address[2] = {0x00, 0x40};
    return write_command((uint8_t)(0x80 | row_address[row] | column));
}

esp_err_t lcd_i2c_write_text(const char *text)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (text == NULL) return ESP_ERR_INVALID_ARG;

    while (*text != '\0') {
        esp_err_t result = write_byte((uint8_t)*text, LCD_RS);
        if (result != ESP_OK) return result;
        // A small margin makes slow HD44780-compatible clones more reliable.
        vTaskDelay(pdMS_TO_TICKS(1));
        ++text;
    }
    return ESP_OK;
}
