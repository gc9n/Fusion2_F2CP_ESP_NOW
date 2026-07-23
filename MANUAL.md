# Fusion2_F2CP_ESP_NOW Manual

## 1. Purpose

Fusion2_F2CP_ESP_NOW turns a Wi-Fi-capable ESP32-family board into a headless
Fusion2 controller. A user enters commands through a serial terminal. The board
encodes those commands as secure F2CP packets and sends them to Fusion2 through
ESP-NOW.

No display, buttons or web server are required on the controller.

## 2. Hardware requirements

- A Wi-Fi-capable ESP32-family development board with ESP-NOW support
- A USB connection for flashing and the serial console
- Fusion2 firmware with F2CP ESP-NOW Control enabled

Included PlatformIO configurations cover classic ESP32, ESP32-C3 and ESP32-S3.
Boards without 2.4 GHz Wi-Fi cannot use ESP-NOW and are not compatible.

## 3. Project structure

```text
Fusion2_F2CP_ESP_NOW/
├── platformio.ini
├── README.md
├── MANUAL.md
├── GITHUB_DESCRIPTION.txt
├── include/
│   └── Fusion2F2CP.h
├── examples/
│   └── TuneFromCode.cpp
└── src/
    └── main.cpp
```

The firmware is intentionally compact. `main.cpp` contains the ESP-NOW
transport, pairing, session security, NVS storage, serial parser and F2CP command
handling. `include/Fusion2F2CP.h` exposes the small application-facing API for
sending commands directly from firmware code.

## 4. Selecting the board

Open `platformio.ini` and select the environment that matches the board.

| Environment | Typical hardware |
|---|---|
| `esp32dev-uart` | Classic ESP32 DevKit with USB-to-UART |
| `esp32-c3-uart` | ESP32-C3 with CH340/CP210x |
| `esp32-c3-native-usb` | ESP32-C3 native USB Serial/JTAG |
| `esp32-s3-uart` | ESP32-S3 with CH340/CP210x |
| `esp32-s3-native-usb` | ESP32-S3 native USB Serial/JTAG |

For another compatible ESP32-family board, copy the closest environment and
change only its `board` value.

Example:

```ini
[env:my-esp32-board]
board = your-platformio-board-id
```

Use the native-USB flags only when the USB connector is wired directly to the
ESP32 chip. Boards with a CH340 or CP210x should use a UART environment.

## 5. Build and upload

Example for classic ESP32:

```bash
pio run -e esp32dev-uart -t upload
pio device monitor -e esp32dev-uart
```

Example for ESP32-S3 native USB:

```bash
pio run -e esp32-s3-native-usb -t upload
pio device monitor -e esp32-s3-native-usb
```

The serial speed is 115200 baud.

## 6. Pairing procedure

1. Flash Fusion2 with F2CP support.
2. Enable `ESP-NOW Control` in Fusion2 Settings.
3. Flash this controller firmware.
4. Open the controller serial terminal.
5. On Fusion2 open `F2CP Pair 60s`.
6. Fusion2 selects its ESP-NOW channel and advertises the open window.
7. The controller scans channels 1 through 13 until it finds Fusion2.
8. The controller prints the six-digit pairing code.
9. Verify that the serial code matches the Fusion2 OLED code.
10. The devices exchange and save a unique pair key.
11. The controller establishes the secure F2CP session.

Expected output resembles:

```text
PAIR WINDOW FOUND: code 482917, Wi-Fi channel 6.
Fusion2 accepted request. Waiting for PAIR COMPLETE...
PAIR COMPLETE received. Unique peer key saved.
PAIRING COMPLETE on Wi-Fi channel 6.
```

## 7. Reconnection

The controller stores:

- Fusion2 MAC address
- Unique long-term pair key
- Last working Wi-Fi channel

After reboot it probes the Wi-Fi channels until the paired Fusion2 responds,
then creates a fresh secure session.

## 8. Commands

### Status

```text
status
```

Requests the current Fusion2 state, frequency, channel, RSSI and active
operation.

### Current frequency

```text
get
```

Queries the actual frequency and RSSI reported by Fusion2.

### Tune

```text
tune 5880
```

Requests an exact VRX frequency in MHz.

### Start spectrum scan

```text
scan 5645 5950 10
```

Arguments are start frequency, stop frequency and step size in MHz.

### Stop scan

```text
scanstop
```

### AutoLock

```text
autolock
next up
next down
accept
cancel
```

`autolock` starts the search. `next` searches for a different signal. `accept`
keeps the found frequency. `cancel` stops the operation.

### Remove pairing

```text
forget
```

Erases the saved Fusion2 peer from the controller. Open a new 60-second Fusion2
pairing window to pair again.

### Help

```text
help
```

## 9. Sending commands directly from code

The serial parser is only one way to use the controller. Firmware code can call
the public API declared in:

```text
include/Fusion2F2CP.h
```

Available functions in this release:

```cpp
bool Fusion2F2CP_IsPaired();
bool Fusion2F2CP_IsConnected();
bool Fusion2F2CP_Tune(uint16_t mhz, bool save = true);
```

### One-shot tune example

```cpp
#include "Fusion2F2CP.h"

void loop()
{
    static bool commandSent = false;

    if (!commandSent && Fusion2F2CP_IsConnected()) {
        commandSent = Fusion2F2CP_Tune(5880, true);
    }
}
```

This sends the same F2CP tune request as:

```text
tune 5880
```

but it does not use the serial terminal or serial command parser.

The `save` parameter controls Fusion2 persistence:

```cpp
Fusion2F2CP_Tune(5880, true);   // Tune and save in Fusion2
Fusion2F2CP_Tune(5880, false);  // Temporary tune, do not save
```

The function waits for Fusion2's acknowledgement and returns `true` when the
expected response is received. It returns `false` when there is no active secure
session, the frequency is invalid or Fusion2 does not acknowledge the command.

### Triggering from application logic

The same call can be placed behind a GPIO event, timer, sensor threshold or
application state transition:

```cpp
if (buttonPressed && Fusion2F2CP_IsConnected()) {
    Fusion2F2CP_Tune(5800, true);
}
```

Do not call the command repeatedly on every pass through `loop()`. Use a
one-shot flag or edge detection. The command API is blocking while it waits for
the response, so call it only from normal loop/task context—not from an ISR and
not directly from an ESP-NOW receive callback.

A retry-safe implementation is provided in:

```text
examples/TuneFromCode.cpp
```

## 10. Fusion2 events

Fusion2 can communicate with the controller without waiting for a new serial
command. The serial terminal may receive:

```text
EVENT: 5880 MHz R7, A=82 B=79
SCAN offset=0 count=20 ...
AUTOLOCK FOUND: 5880 MHz R7, A=88 B=84
OPERATION: 2 START
```

## 11. Wi-Fi channel behavior

During pairing, the controller scans channels 1 through 13. After pairing, it
stores the last successful channel. If Fusion2 later changes channel, the
controller probes all channels until it restores the secure session.

When Fusion2 uses legacy ELRS Backpack compatibility, Fusion2 may force channel
1. The controller does not require a fixed channel and will still find it.

## 12. Troubleshooting

### `Serial` is not declared

The wrong USB environment was selected. Use a `native-usb` environment only for
boards whose USB connector is wired directly to the ESP32 chip. Use a `uart`
environment for CH340/CP210x boards.

Clean before rebuilding:

```bash
pio run -t clean
```

### Pairing window is not detected

- Flash matching F2CP versions on Fusion2 and the controller.
- Open the Fusion2 `F2CP Pair 60s` page.
- Run `forget` when testing after a protocol change.
- Verify that ESP-NOW Control is enabled on Fusion2.
- Keep the devices near each other during the first test.

### Paired but not connected

Reboot both devices. The controller will scan all channels and attempt to restore
the secure session.

### Wrong PlatformIO board

Find the board's PlatformIO identifier and create a new environment by copying
the nearest supplied configuration.

## 13. Extending the project

New F2CP commands can be added to the serial parser in `runCommand()` and decoded
in `printResponse()`. Existing command IDs and packet formats should remain
stable so older controllers continue to operate with newer Fusion2 firmware.
