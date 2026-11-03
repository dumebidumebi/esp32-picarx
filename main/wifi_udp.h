#pragma once

// FreeRTOS.h must come before the individual queue header.
#include "freertos/FreeRTOS.h"

// Gives us QueueHandle_t for the joystick-reading queue.
#include "freertos/queue.h"

// Connect to Wi-Fi and send queue readings to the Raspberry Pi forever.
// This function has the correct shape to run as a FreeRTOS task.
void wifi_udp_task(void *argument);
