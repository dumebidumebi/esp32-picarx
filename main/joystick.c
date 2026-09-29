#include "joystick.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define JOYSTICK_X_CHANNEL ADC_CHANNEL_6  // GPIO34
#define JOYSTICK_Y_CHANNEL ADC_CHANNEL_7  // GPIO35
#define JOYSTICK_SW_GPIO   GPIO_NUM_32

static adc_oneshot_unit_handle_t adc_handle;
static bool last_raw_button;
static bool stable_button;
static int matching_button_samples;

esp_err_t joystick_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t result = adc_oneshot_new_unit(&unit_config, &adc_handle);
    if (result != ESP_OK) return result;

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    result = adc_oneshot_config_channel(adc_handle, JOYSTICK_X_CHANNEL,
                                        &channel_config);
    if (result != ESP_OK) return result;

    result = adc_oneshot_config_channel(adc_handle, JOYSTICK_Y_CHANNEL,
                                        &channel_config);
    if (result != ESP_OK) return result;

    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << JOYSTICK_SW_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&button_config);
}

esp_err_t joystick_read(joystick_reading_t *reading)
{
    if (reading == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t result = adc_oneshot_read(adc_handle, JOYSTICK_X_CHANNEL,
                                        &reading->raw_x);
    if (result != ESP_OK) return result;

    result = adc_oneshot_read(adc_handle, JOYSTICK_Y_CHANNEL,
                              &reading->raw_y);
    if (result != ESP_OK) return result;

    // The switch connects GPIO32 to ground when it is pressed.
    bool raw_button = gpio_get_level(JOYSTICK_SW_GPIO) == 0;

    // Accept a new button state after three matching samples.
    if (raw_button == last_raw_button) {
        if (matching_button_samples < 3) matching_button_samples++;
    } else {
        last_raw_button = raw_button;
        matching_button_samples = 1;
    }

    if (matching_button_samples >= 3) stable_button = raw_button;
    reading->button_pressed = stable_button;

    return ESP_OK;
}
