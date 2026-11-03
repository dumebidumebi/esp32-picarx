// This is our own header file. It describes the public joystick functions.
#include "joystick.h"

// Lets us configure and read normal GPIO pins.
#include "driver/gpio.h"

// Lets us read analog voltages with the ESP32 ADC.
#include "esp_adc/adc_oneshot.h"

// GPIO34 is ADC1 channel 6. It reads the joystick's X output.
#define JOYSTICK_X_CHANNEL ADC_CHANNEL_6  // GPIO34

// GPIO35 is ADC1 channel 7. It reads the joystick's Y output.
#define JOYSTICK_Y_CHANNEL ADC_CHANNEL_7  // GPIO35

// GPIO32 reads the joystick's push-button output.
#define JOYSTICK_SW_GPIO   GPIO_NUM_32

// ESP-IDF gives us this handle when the ADC is created.
// We use the handle later when we want to read an ADC channel.
static adc_oneshot_unit_handle_t adc_handle;

// These variables remember recent button readings for software debounce.
// Static variables keep their values between function calls.
static bool last_raw_button;
static bool stable_button;
static int matching_button_samples;

// Set up all hardware needed by the joystick.
esp_err_t joystick_init(void)
{
    // Describe which ADC unit we want to use.
    adc_oneshot_unit_init_cfg_t unit_config = {
        // Both joystick axes use ADC unit 1.
        .unit_id = ADC_UNIT_1,
    };

    // Create ADC unit 1.
    // &unit_config passes the address of our settings into the function.
    // &adc_handle lets the function save the new handle for us.
    esp_err_t result = adc_oneshot_new_unit(&unit_config, &adc_handle);

    // Stop and return the error if the ADC could not be created.
    if (result != ESP_OK) return result;

    // Describe how both joystick ADC channels should work.
    adc_oneshot_chan_cfg_t channel_config = {
        // Use the larger input range supported by this ADC setting.
        .atten = ADC_ATTEN_DB_12,

        // Let ESP-IDF choose the normal ADC bit width for this ESP32.
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    // Apply the channel settings to the X axis.
    result = adc_oneshot_config_channel(adc_handle, JOYSTICK_X_CHANNEL,
                                        &channel_config);

    // Stop if the X channel could not be configured.
    if (result != ESP_OK) return result;

    // Apply the same channel settings to the Y axis.
    result = adc_oneshot_config_channel(adc_handle, JOYSTICK_Y_CHANNEL,
                                        &channel_config);

    // Stop if the Y channel could not be configured.
    if (result != ESP_OK) return result;

    // Describe how the joystick push-button pin should work.
    gpio_config_t button_config = {
        // Select only GPIO32. Each bit represents one GPIO number.
        .pin_bit_mask = 1ULL << JOYSTICK_SW_GPIO,

        // We read this pin, so it is an input.
        .mode = GPIO_MODE_INPUT,

        // Keep the pin HIGH while the button is not pressed.
        .pull_up_en = GPIO_PULLUP_ENABLE,

        // We do not need a pull-down resistor at the same time.
        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        // This program checks the button regularly instead of using an interrupt.
        .intr_type = GPIO_INTR_DISABLE,
    };

    // Configure the button and return ESP_OK or the error that occurred.
    return gpio_config(&button_config);
}

// Read both joystick axes and the push button.
// The pointer lets this function write into the caller's structure.
esp_err_t joystick_read(joystick_reading_t *reading)
{
    // A NULL pointer does not point to a real structure, so reject it safely.
    if (reading == NULL) return ESP_ERR_INVALID_ARG;

    // Read the X voltage and save the raw ADC number in reading->raw_x.
    esp_err_t result = adc_oneshot_read(adc_handle, JOYSTICK_X_CHANNEL,
                                        &reading->raw_x);

    // Do not continue if the X read failed.
    if (result != ESP_OK) return result;

    // Read the Y voltage and save the raw ADC number in reading->raw_y.
    result = adc_oneshot_read(adc_handle, JOYSTICK_Y_CHANNEL,
                              &reading->raw_y);

    // Do not continue if the Y read failed.
    if (result != ESP_OK) return result;

    // The switch connects GPIO32 to ground when it is pressed.
    // Therefore, a LOW pin means the button is pressed.
    bool raw_button = gpio_get_level(JOYSTICK_SW_GPIO) == 0;

    // Compare this sample with the last sample for software debounce.
    if (raw_button == last_raw_button) {
        // Count repeated samples, but stop counting after three.
        if (matching_button_samples < 3) matching_button_samples++;
    } else {
        // The raw state changed, so start counting again.
        last_raw_button = raw_button;
        matching_button_samples = 1;
    }

    // Accept the new button state only after three matching samples.
    // This filters out fast electrical bouncing when the button moves.
    if (matching_button_samples >= 3) stable_button = raw_button;

    // Copy the filtered button state into the caller's reading structure.
    reading->button_pressed = stable_button;

    // Tell the caller that every joystick value was read successfully.
    return ESP_OK;
}

// Convert one joystick sample into a word for the LCD and network packet.
const char *joystick_get_direction(const joystick_reading_t *reading)
{
    // Reject a bad pointer safely.
    if (reading == NULL) return "UNKNOWN";

    // Check Y first for forward and backward movement.
    if (reading->raw_y > 2800) return "UP";
    if (reading->raw_y < 1300) return "DOWN";

    // Then check X for left and right movement.
    if (reading->raw_x < 1300) return "LEFT";
    if (reading->raw_x > 2800) return "RIGHT";

    // Values inside the dead zone mean the joystick is centered.
    return "CENTER";
}
