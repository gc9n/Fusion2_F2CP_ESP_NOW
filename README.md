# Fusion2_F2CP_ESP_NOW

**Fusion2_F2CP_ESP_NOW** is a headless serial controller for Fusion2 built on
Wi-Fi-capable boards from the ESP32 family. It sends F2CP commands over ESP-NOW
and prints Fusion2 responses and live events through the board's serial console.

The project is not tied to one specific ESP32 model. The protocol implementation
uses standard Arduino-ESP32 interfaces for Wi-Fi, ESP-NOW, NVS storage, random
number generation and serial I/O.

PlatformIO presets are included for:

- Classic ESP32 boards
- ESP32-C3 boards
- ESP32-S3 boards
- UART-based USB connections
- Native USB Serial/JTAG connections where supported

Other Wi-Fi-capable ESP32-family boards can be used by adding the appropriate
PlatformIO board ID. A board must provide 2.4 GHz Wi-Fi and ESP-NOW support.

## Features

- Automatic discovery of the Fusion2 60-second pairing window
- Multi-channel pairing and reconnect scanning
- No manually entered pairing key
- Persistent encrypted one-to-one pairing in NVS
- Secure F2CP session establishment after reboot
- Exact-frequency tuning
- Spectrum scan start/stop and scan-result reception
- AutoLock start, next, accept and cancel commands
- Live frequency, scan, AutoLock and operation events from Fusion2
- Headless operation through a standard serial terminal

## Repository description

> Headless ESP32-family serial controller for Fusion2 using secure F2CP over ESP-NOW. Supports automatic pairing, channel discovery, tuning, spectrum scans, AutoLock and live Fusion2 events.

## Choose a PlatformIO environment

Use the environment that matches the board and its USB connection.

### Classic ESP32 with USB-to-UART

```bash
pio run -e esp32dev-uart -t upload
pio device monitor -e esp32dev-uart
```

### ESP32-C3 with USB-to-UART

```bash
pio run -e esp32-c3-uart -t upload
pio device monitor -e esp32-c3-uart
```

### ESP32-C3 with native USB

```bash
pio run -e esp32-c3-native-usb -t upload
pio device monitor -e esp32-c3-native-usb
```

### ESP32-S3 with USB-to-UART

```bash
pio run -e esp32-s3-uart -t upload
pio device monitor -e esp32-s3-uart
```

### ESP32-S3 with native USB

```bash
pio run -e esp32-s3-native-usb -t upload
pio device monitor -e esp32-s3-native-usb
```

For another ESP32-family board, duplicate the closest environment in
`platformio.ini` and replace its `board` value with the correct PlatformIO board
ID.

## Pairing

1. Flash the controller and open its serial monitor at 115200 baud.
2. On Fusion2, enable **ESP-NOW Control**.
3. Open **Settings > F2CP Pair 60s** on Fusion2.
4. Fusion2 displays a six-digit code and the remaining pairing time.
5. The ESP32 controller scans the available 2.4 GHz channels and detects the
   open pairing window automatically.
6. Confirm that the code printed in the serial terminal matches the Fusion2
   OLED code.
7. Pairing completes automatically and both devices store the unique peer key.
8. Future boots reconnect automatically without reopening the pairing window.

The pairing code is a visual confirmation only. It is not typed into the
controller.

## Serial commands

```text
status
get
tune 5880
scan 5645 5950 10
scanstop
autolock
next up
next down
accept
cancel
forget
help
```

## Communication direction

The controller sends serial commands to Fusion2 over ESP-NOW. Fusion2 also sends
responses and unsolicited events back to the controller, including:

- Frequency changes
- RSSI values
- Spectrum data and completion
- AutoLock results
- Operation-state changes

## Security

The predefined bootstrap material is used only while Fusion2 has an explicitly
opened 60-second pairing window. After pairing, the two devices use a unique
saved peer key, encrypted ESP-NOW unicast, HMAC authentication, session IDs,
sequence counters and replay protection.

Pairing is currently one-to-one. Pairing another controller replaces the
previous controller.

## Documentation

See [MANUAL.md](MANUAL.md) for complete setup, pairing, command and
troubleshooting instructions.
