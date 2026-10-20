// This is our own header file. It describes the public LCD functions.
#include "lcd_i2c.h"

// Gives us the bool type for true and false values.
#include <stdbool.h>

// Gives us NULL and size-related definitions.
#include <stddef.h>

// Lets us use the ESP-IDF I2C master driver.
#include "driver/i2c_master.h"

// Lets us print useful messages to the serial monitor.
#include "esp_log.h"

// Gives us a short microsecond delay for the LCD Enable signal.
#include "esp_rom_sys.h"

// Gives us FreeRTOS timing definitions.
#include "freertos/FreeRTOS.h"

// Lets the current FreeRTOS task wait without blocking the whole ESP32.
#include "freertos/task.h"

// Use the ESP32's I2C controller number 0.
#define LCD_I2C_PORT       I2C_NUM_0

// GPIO21 carries I2C data between the ESP32 and LCD backpack.
#define LCD_SDA_GPIO       21

// GPIO22 carries the I2C clock between the ESP32 and LCD backpack.
#define LCD_SCL_GPIO       22

// Run the I2C bus at the standard 100 kHz speed.
#define LCD_I2C_FREQUENCY  100000

// Stop waiting after 100 ms if an I2C transfer does not finish.
#define LCD_TIMEOUT_MS     100

// Common PCF8574 backpack wiring:
// P0=RS, P1=RW, P2=Enable, P3=Backlight, P4-P7=LCD data D4-D7.
// This bit controls whether a byte is a command or a character.
#define LCD_RS        0x01

// This bit creates the Enable pulse that makes the LCD save data.
#define LCD_ENABLE    0x04

// This bit turns on the LCD backlight through the I2C backpack.
#define LCD_BACKLIGHT 0x08

// This label appears beside messages from this file in the serial monitor.
static const char *TAG = "lcd_i2c";

// These handles let us refer to the I2C bus and LCD device later.
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t lcd_handle;

// This becomes true only after the LCD setup finishes successfully.
static bool initialized;

// This value is added to each backpack write to keep the backlight on or off.
static uint8_t backlight_value;

// This stores the I2C address found during setup.
static uint8_t detected_address;

// Send one byte to the PCF8574 I2C backpack.
static esp_err_t expander_write(uint8_t value)
{
    // Add the current backlight bit without changing the other control bits.
    value |= backlight_value;

    // Send the byte to the LCD backpack and return the result.
    return i2c_master_transmit(lcd_handle, &value, 1, LCD_TIMEOUT_MS);
}

// Pulse the LCD Enable pin so the LCD saves the four data bits.
static esp_err_t pulse_enable(uint8_t value)
{
    // Match Freenove's driver: put the nibble on D4-D7 while Enable is high,
    // then lower Enable so the LCD saves that nibble.
    esp_err_t result = expander_write(value | LCD_ENABLE);

    // Stop immediately if the first I2C write failed.
    if (result != ESP_OK) return result;

    // Keep Enable HIGH long enough for the LCD to see it.
    esp_rom_delay_us(10);

    // Lower Enable. The falling edge tells the LCD to save the nibble.
    result = expander_write(value & (uint8_t)~LCD_ENABLE);

    // Give the LCD time to process the nibble.
    esp_rom_delay_us(100);

    // Return ESP_OK or the I2C error from lowering Enable.
    return result;
}

// Send four data bits to the LCD.
static esp_err_t write_nibble(uint8_t nibble, uint8_t mode)
{
    // Move the four data bits into backpack pins P4 through P7.
    // The mode contains the RS bit: command mode or character mode.
    uint8_t value = (uint8_t)((nibble & 0x0F) << 4) | mode;

    // Pulse Enable so the LCD reads these four bits.
    return pulse_enable(value);
}

// Send a full eight-bit value as two four-bit pieces.
static esp_err_t write_byte(uint8_t value, uint8_t mode)
{
    // Send the upper four bits first.
    esp_err_t result = write_nibble(value >> 4, mode);
    if (result != ESP_OK) return result;

    // Then send the lower four bits.
    return write_nibble(value, mode);
}

// Send one instruction byte to the LCD controller.
static esp_err_t write_command(uint8_t command)
{
    // Mode 0 means this byte is a command, not a visible character.
    esp_err_t result = write_byte(command, 0);

    // Clear and Home are slower than normal LCD commands.
    if (command == 0x01 || command == 0x02) {
        // Let this FreeRTOS task wait while the LCD finishes the command.
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // Return the result of sending the command.
    return result;
}

// Set up I2C, find the backpack, and initialize the LCD controller.
esp_err_t lcd_i2c_init(void)
{
    // Describe the ESP32 I2C bus that connects to the LCD.
    i2c_master_bus_config_t bus_config = {
        // Use I2C controller 0.
        .i2c_port = LCD_I2C_PORT,

        // Select the physical SDA and SCL pins.
        .sda_io_num = LCD_SDA_GPIO,
        .scl_io_num = LCD_SCL_GPIO,

        // Let ESP-IDF choose the normal I2C clock source.
        .clk_source = I2C_CLK_SRC_DEFAULT,

        // Ignore extremely short pulses that are probably electrical noise.
        .glitch_ignore_cnt = 7,

        // Enable the ESP32's weak internal pull-up resistors.
        .flags.enable_internal_pullup = true,
    };

    // Create the I2C bus and save its handle in bus_handle.
    esp_err_t result = i2c_new_master_bus(&bus_config, &bus_handle);
    if (result != ESP_OK) return result;

    // Freenove uses 0x27 for PCF8574T and 0x3F for PCF8574AT.
    // Try the most common address first.
    detected_address = 0x27;
    result = i2c_master_probe(bus_handle, detected_address, LCD_TIMEOUT_MS);

    // If 0x27 did not answer, try the other common address.
    if (result != ESP_OK) {
        detected_address = 0x3F;
        result = i2c_master_probe(bus_handle, detected_address, LCD_TIMEOUT_MS);
    }

    // Stop if no LCD backpack answered at either address.
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No LCD answered at 0x27 or 0x3F");
        return result;
    }

    // Print the address that worked so we can see it in the serial monitor.
    ESP_LOGI(TAG, "LCD backpack found at I2C address 0x%02X", detected_address);

    // Describe the LCD backpack as one device on our I2C bus.
    i2c_device_config_t device_config = {
        // The backpack uses a normal seven-bit I2C address.
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,

        // Use the address found by the probe above.
        .device_address = detected_address,

        // Communicate with this device at 100 kHz.
        .scl_speed_hz = LCD_I2C_FREQUENCY,
    };

    // Add the LCD device to the bus and save its device handle.
    result = i2c_master_bus_add_device(bus_handle, &device_config, &lcd_handle);
    if (result != ESP_OK) return result;

    // Follow Freenove's bundled LCD initialization sequence.
    // First, wait for the LCD power to become stable.
    vTaskDelay(pdMS_TO_TICKS(50));

    // Start with the backlight bit off and clear the backpack outputs.
    backlight_value = 0;
    result = expander_write(0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(20));

    // HD44780 power-up sequence: enter 4-bit mode.
    // The repeated 0x03 values reset the LCD into a known state.
    result = write_nibble(0x03, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(5));
    result = write_nibble(0x03, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(1));
    result = write_nibble(0x03, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(1));

    // Sending 0x02 changes the LCD from eight-bit mode to four-bit mode.
    result = write_nibble(0x02, 0);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(1));

    // Two lines/5x8, display on/cursor off, clear, increment, and home.
    // Each hexadecimal number is a command from the HD44780 instruction set.
    if ((result = write_command(0x28)) != ESP_OK) return result;
    if ((result = write_command(0x0C)) != ESP_OK) return result;
    if ((result = write_command(0x01)) != ESP_OK) return result;
    if ((result = write_command(0x06)) != ESP_OK) return result;
    if ((result = write_command(0x02)) != ESP_OK) return result;

    // Turn on the backlight after the LCD controller is ready.
    backlight_value = LCD_BACKLIGHT;
    if ((result = expander_write(0)) != ESP_OK) return result;

    // Public LCD functions may now safely send commands and text.
    initialized = true;
    return ESP_OK;
}

// Clear both rows of the LCD.
esp_err_t lcd_i2c_clear(void)
{
    // Do not use the display before initialization is complete.
    if (!initialized) return ESP_ERR_INVALID_STATE;

    // Command 0x01 clears the screen.
    return write_command(0x01);
}

// Move the cursor to one position on our 16-column, two-row display.
esp_err_t lcd_i2c_set_cursor(uint8_t column, uint8_t row)
{
    // Reject calls made before initialization.
    if (!initialized) return ESP_ERR_INVALID_STATE;

    // Valid columns are 0-15 and valid rows are 0-1.
    if (column >= 16 || row >= 2) return ESP_ERR_INVALID_ARG;

    // The LCD stores the two display rows at different memory addresses.
    static const uint8_t row_address[2] = {0x00, 0x40};

    // Bit 7 tells the LCD to move its cursor to the calculated address.
    return write_command((uint8_t)(0x80 | row_address[row] | column));
}

// Write every character in a C string to the LCD.
esp_err_t lcd_i2c_write_text(const char *text)
{
    // Make sure the LCD is ready and the pointer is valid.
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (text == NULL) return ESP_ERR_INVALID_ARG;

    // A C string ends when we reach its zero byte, written as '\0'.
    while (*text != '\0') {
        // LCD_RS means this byte is visible character data, not a command.
        esp_err_t result = write_byte((uint8_t)*text, LCD_RS);
        if (result != ESP_OK) return result;

        // A small margin makes slow HD44780-compatible clones more reliable.
        vTaskDelay(pdMS_TO_TICKS(1));

        // Move the pointer to the next character in the string.
        ++text;
    }

    // Every character was sent successfully.
    return ESP_OK;
}
