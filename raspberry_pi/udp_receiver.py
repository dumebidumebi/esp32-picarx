#!/usr/bin/env python3
"""Show live ESP32 joystick packets in a Raspberry Pi terminal."""

import json
import socket
import sys
import time


LISTEN_ADDRESS = "0.0.0.0"
LISTEN_PORT = 3333
ADC_MAX = 4095


def make_bar(value: int, width: int = 30) -> str:
    """Convert an ADC value into a fixed-width terminal bar."""
    safe_value = max(0, min(ADC_MAX, value))
    filled = round((safe_value / ADC_MAX) * width)
    return "#" * filled + "-" * (width - filled)


def draw_dashboard(packet: dict, source: tuple[str, int], count: int) -> None:
    """Redraw the same terminal area with the newest valid packet."""
    x_value = int(packet["x"])
    y_value = int(packet["y"])
    pressed = bool(packet["button"])
    direction = str(packet["direction"])
    sequence = int(packet["sequence"])
    uptime_ms = int(packet["uptime_ms"])

    # Move the cursor home and clear everything below it.
    sys.stdout.write("\033[H\033[J")
    print("ESP32 PiCar-X Wireless Controller Demo")
    print("=" * 48)
    print("Status:        LIVE UDP DATA")
    print(f"ESP32 source:  {source[0]}:{source[1]}")
    print(f"Packets seen:  {count}")
    print(f"Sequence:      {sequence}")
    print(f"ESP32 uptime:  {uptime_ms / 1000:.1f} seconds")
    print()
    print(f"X: {x_value:4d}  [{make_bar(x_value)}]")
    print(f"Y: {y_value:4d}  [{make_bar(y_value)}]")
    print()
    print(f"Direction:     {direction}")
    print(f"Button:        {'PRESSED' if pressed else 'released'}")
    print()
    print("Press Ctrl+C to stop the receiver.")
    sys.stdout.flush()


def main() -> None:
    """Listen forever and display valid JSON packets from the ESP32."""
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_socket.bind((LISTEN_ADDRESS, LISTEN_PORT))
    udp_socket.settimeout(1.0)

    packet_count = 0
    last_packet_time = None

    print(f"Waiting for ESP32 UDP packets on port {LISTEN_PORT}...")

    try:
        while True:
            try:
                raw_data, source = udp_socket.recvfrom(2048)
            except socket.timeout:
                if last_packet_time and time.monotonic() - last_packet_time > 2:
                    sys.stdout.write("\033[H\033[J")
                    print("ESP32 PiCar-X Wireless Controller Demo")
                    print("=" * 48)
                    print("Status: WAITING - no packet received for 2 seconds")
                    print(f"Listening on UDP port {LISTEN_PORT}")
                    print("Press Ctrl+C to stop the receiver.")
                    sys.stdout.flush()
                continue

            try:
                packet = json.loads(raw_data.decode("utf-8"))
                required = {"sequence", "x", "y", "button",
                            "direction", "uptime_ms"}
                if not required.issubset(packet):
                    raise ValueError("packet is missing required fields")
            except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                print(f"Ignored invalid packet from {source}: {error}")
                continue

            packet_count += 1
            last_packet_time = time.monotonic()
            draw_dashboard(packet, source, packet_count)

    except KeyboardInterrupt:
        print("\nReceiver stopped.")
    finally:
        udp_socket.close()


if __name__ == "__main__":
    main()
