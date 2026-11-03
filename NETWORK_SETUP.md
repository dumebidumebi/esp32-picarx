# ESP32 to Raspberry Pi UDP Demo

The ESP32 sends live joystick readings to the Raspberry Pi as JSON over UDP port 3333. The Pi displays the readings in its terminal. The Robot HAT is not required for this demo.

## 1. Find the Raspberry Pi IP address

Run this inside the existing SSH session:

```bash
hostname -I | awk '{print $1}'
```

Keep the first IPv4 address that is printed.

## 2. Configure the ESP32 locally

Edit `main/network_config.h` on the Mac and replace the three placeholder values:

```c
#define WIFI_SSID       "YOUR_2_4_GHZ_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define PI_IP_ADDRESS   "THE_PI_IP_FROM_STEP_1"
```

`network_config.h` is ignored by Git, so the password will not be uploaded. The ESP32 and Raspberry Pi must be connected to the same network. The original ESP32 uses 2.4 GHz Wi-Fi.

## 3. Copy the receiver from the Mac to the Pi

Run this in a regular Mac terminal, not inside the SSH session:

```bash
scp "/Users/chidumebinkata/Downloads/esp32 picar controller/esp32_picar_controller/raspberry_pi/udp_receiver.py" pi@picarx.local:~/udp_receiver.py
```

## 4. Start the receiver on the Pi

Run this inside the SSH session:

```bash
python3 ~/udp_receiver.py
```

It should first say that it is waiting for packets on UDP port 3333.

## 5. Build and flash the ESP32

Run this in an ESP-IDF terminal on the Mac:

```bash
cd "/Users/chidumebinkata/Downloads/esp32 picar controller/esp32_picar_controller"
idf.py build
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

After Wi-Fi connects, the ESP32 monitor prints its IP address and the Pi terminal begins showing X, Y, direction, button state, sequence number, and packet status.

Use `Ctrl+C` to stop the Python receiver. Use `Ctrl+]` to leave the ESP-IDF serial monitor.
