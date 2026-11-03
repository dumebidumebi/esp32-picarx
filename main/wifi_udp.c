// This is our public network module header.
#include "wifi_udp.h"

// Gives us errno values when a socket operation fails.
#include <errno.h>

// Gives us snprintf(), which creates the JSON packet text.
#include <stdio.h>

// Gives us memset() and strlen().
#include <string.h>

// Gives us fixed-size integer types such as uint32_t.
#include <stdint.h>

// ESP-IDF event and Wi-Fi libraries.
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

// FreeRTOS event groups let the sender wait for a Wi-Fi connection.
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// BSD-style UDP socket functions supplied by lwIP.
#include "lwip/inet.h"
#include "lwip/sockets.h"

// Our project types and private local network settings.
#include "joystick.h"
#include "network_config.h"

// Bit 0 becomes 1 after the ESP32 receives an IP address.
#define WIFI_CONNECTED_BIT BIT0

// Name shown beside network messages in the serial monitor.
static const char *TAG = "wifi_udp";

// Stores the FreeRTOS connection-status bits.
static EventGroupHandle_t wifi_events;

// ESP-IDF stores these registrations so they can be removed later if needed.
static esp_event_handler_instance_t wifi_handler_instance;
static esp_event_handler_instance_t ip_handler_instance;

// Respond to important Wi-Fi and IP events from ESP-IDF.
static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    // Start connecting after the Wi-Fi station starts.
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    // If Wi-Fi drops, clear the connected flag and try again.
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        esp_wifi_connect();
        return;
    }

    // An IP address means the ESP32 can now send packets on the network.
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "ESP32 IP address: " IPSTR,
                 IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

// Initialize nonvolatile storage, which the Wi-Fi driver uses internally.
static esp_err_t initialize_nvs(void)
{
    esp_err_t result = nvs_flash_init();

    // Repair NVS when an old or full storage layout is found.
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }

    return result;
}

// Configure the ESP32 as a station and begin connecting to the router.
static esp_err_t initialize_wifi(void)
{
    ESP_RETURN_ON_ERROR(initialize_nvs(), TAG, "NVS setup failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network interface setup failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "Event loop setup failed");

    // Create the normal Wi-Fi station network interface.
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (station == NULL) return ESP_FAIL;

    // Create the Wi-Fi driver with Espressif's recommended defaults.
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG,
                        "Wi-Fi driver setup failed");

    // Send all Wi-Fi and acquired-IP events to our handler.
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_event_handler, NULL,
                                            &wifi_handler_instance),
        TAG, "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifi_event_handler, NULL,
                                            &ip_handler_instance),
        TAG, "IP event registration failed");

    // Copy the local SSID and password into ESP-IDF's Wi-Fi structure.
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "Could not select station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG,
                        "Could not apply Wi-Fi settings");
    return esp_wifi_start();
}

// Open one IPv4 UDP socket aimed at the Raspberry Pi.
static int create_udp_socket(struct sockaddr_in *destination)
{
    memset(destination, 0, sizeof(*destination));
    destination->sin_family = AF_INET;
    destination->sin_port = htons(PI_UDP_PORT);

    // Convert the written IPv4 address into the binary form used by sockets.
    if (inet_pton(AF_INET, PI_IP_ADDRESS, &destination->sin_addr) != 1) {
        ESP_LOGE(TAG, "Invalid Raspberry Pi IP address: %s", PI_IP_ADDRESS);
        return -1;
    }

    int socket_number = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_number < 0) {
        ESP_LOGE(TAG, "Could not create UDP socket: errno %d", errno);
        return -1;
    }

    ESP_LOGI(TAG, "Sending UDP readings to %s:%d",
             PI_IP_ADDRESS, PI_UDP_PORT);
    return socket_number;
}

// Connect to Wi-Fi, receive joystick samples, and transmit JSON packets.
void wifi_udp_task(void *argument)
{
    // app_main passes this task its own one-item joystick queue.
    QueueHandle_t reading_queue = (QueueHandle_t)argument;
    joystick_reading_t reading;
    struct sockaddr_in destination;
    char packet[192];
    uint32_t sequence = 0;

    // Create the event group before Wi-Fi events can arrive.
    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        ESP_LOGE(TAG, "Could not create Wi-Fi event group");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t result = initialize_wifi();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi setup failed: %s", esp_err_to_name(result));
        vTaskDelete(NULL);
        return;
    }

    // Wait until DHCP gives the ESP32 an IP address.
    ESP_LOGI(TAG, "Connecting to Wi-Fi network: %s", WIFI_SSID);
    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    int socket_number = create_udp_socket(&destination);
    if (socket_number < 0) {
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // Wait for the newest sample produced by joystick_task().
        if (xQueueReceive(reading_queue, &reading, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Pause sending while Wi-Fi is reconnecting.
        xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        // Build a human-readable JSON message for the Python receiver.
        int length = snprintf(
            packet, sizeof(packet),
            "{\"sequence\":%lu,\"x\":%d,\"y\":%d,\"button\":%s,"
            "\"direction\":\"%s\",\"uptime_ms\":%lld}",
            (unsigned long)sequence++, reading.raw_x, reading.raw_y,
            reading.button_pressed ? "true" : "false",
            joystick_get_direction(&reading),
            (long long)(esp_timer_get_time() / 1000));

        if (length <= 0 || length >= (int)sizeof(packet)) {
            ESP_LOGE(TAG, "UDP packet did not fit in the buffer");
            continue;
        }

        int sent = sendto(socket_number, packet, length, 0,
                          (struct sockaddr *)&destination,
                          sizeof(destination));
        if (sent < 0) {
            ESP_LOGE(TAG, "UDP send failed: errno %d", errno);
        }

        // Ten network updates per second are smooth enough for a terminal demo.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
