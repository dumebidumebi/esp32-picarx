# ESP32 PiCar Controller — Saved State

Last verified: September 29, 2026

## Current result

The Freenove 16x2 I2C LCD is working. It displays:

```text
LINE 1: HELLO
LINE 2: 12345
```

The LCD was previously showing 16 solid blocks on the first row. That meant it had power and contrast but had not initialized. The driver was fixed by matching Freenove's two-write Enable-pulse sequence and using longer startup timing.

## Verified LCD wiring

- LCD GND -> ESP32 GND
- LCD VDD -> ESP32 VIN/5V
- LCD SDA -> ESP32 GPIO21
- LCD SCL -> ESP32 GPIO22
- Detected I2C address: `0x27`

Important: Do not connect LCD VDD to `VN` near GPIO34. `VN` is an analog input, not a power output.

## Project files

- Main program: `main/esp32_picar_controller.c`
- LCD driver: `main/lcd_i2c.c`
- LCD header: `main/lcd_i2c.h`
- Component build file: `main/CMakeLists.txt`

## Build and flash

```bash
cd "/Users/chidumebinkata/Downloads/esp32 picar controller/esp32_picar_controller"
idf.py build
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

Exit the serial monitor with `Ctrl-]`.

If LCD initialization behaves incorrectly after flashing, disconnect USB power for five seconds and reconnect it so the ESP32 and LCD start together.

## Next step

Reconnect the joystick, preserve the working LCD driver, and display live joystick X/Y readings and the interpreted direction on the two LCD rows. Keep the joystick and LCD logic in separate modules so `app_main()` remains easy to read.
