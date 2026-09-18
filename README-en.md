# EoRa-S3-900TB private sensor network

[🇻🇳 Tiếng Việt](README.md) · [🇬🇧 English](README-en.md)

Firmware for a battery-powered EoRa-S3-900TB sensor node and a USB-connected EoRa-S3-900TB Orange Pi gateway. It is a private point-to-multipoint LoRa network: no mesh, LoRaWAN, cloud, or Home Assistant dependency.

## Firmware roles

- `EoRa_Node_Transmitter`: wakes on a timer, collects data, sends an uplink, receives an ACK/at most one command during a short window, then deep-sleeps.
- `EoRa_Node_Receiver_Gateway`: stays awake, bridges valid LoRa uplinks to Orange Pi USB serial, queues Orange Pi commands, and hosts a local Wi-Fi diagnostics page.

The EoRa-S3-900TB SX1262 pins are fixed: SCLK 5, MISO 3, MOSI 6, CS 7, DIO1 33, BUSY 34, RST 8.

## Radio profile: Vietnam (Narrow)

Every peer must be flashed with this exact fixed profile: **920.250 MHz, 62.5 kHz, SF8, CR 4/5, private SX126x sync word, 22 dBm, preamble 16**. The private sync word separates packets; it is **not encryption**. Confirm 22 dBm plus the installed antenna/cable system complies with applicable Vietnam conducted-power/ERP limits before RF use.

## Build and upload

1. Install Arduino IDE 2.x with an ESP32 Arduino Core version compatible with the EoRa-S3-900TB ESP32-S3 board.
2. Install these libraries through Library Manager:
   - **RadioLib** 7.x;
   - **ArduinoJson** 7.x;
   - **U8g2** for the Transmitter OLED.
3. Open the `.ino` file in each sketch directory with Arduino IDE.
4. Select the appropriate EoRa-S3-900TB ESP32-S3 board/USB port and upload.
5. Attach the correctly matched 50-ohm antenna before transmitting.

The gateway must remain USB-powered. Connect its USB serial device (for example `/dev/ttyACM0`) at 115200 baud.

### Confirmed build status

`EoRa_Node_Transmitter` has been built and flashed successfully on physical hardware with ESP32 Arduino Core 3.3.11, RadioLib 7.7.1, and ArduinoJson 7.4.3. The gateway still requires a separate hardware build/flash and verification.

## Transmitter operation

By default, the node sleeps for **300 seconds (5 minutes)**. Each timer wake restarts the firmware from `setup()` and performs the following steps:

1. Enable USB Serial DEBUG and disable Wi-Fi and Bluetooth.
2. Turn on the OLED and show `WAKE`.
3. Initialize the SX1262 with the Vietnam Narrow profile.
4. Wait for a random 0–10 second jitter to reduce collisions among multiple nodes.
5. Increment the RTC-RAM sequence, read the GPIO 1 battery ADC, and build an uplink JSON packet.
6. Send the uplink, retrying at most three times after a failed transmission.
7. Listen for up to five seconds for `ACK`, `CMD`, `TIMEOUT`, or a receive error.
8. Turn off the OLED, sleep the SX1262, and enter deep sleep. A valid `set_interval` command can change the next sleep interval; it survives deep sleep but not complete power loss.

### USB Serial DEBUG

`DEBUG` is enabled by default in `EoRa_Node_Transmitter.ino`. Open Serial Monitor at **115200 baud** to view boot/wake reason, radio initialization, battery, sequence, TX state, ACK/CMD/timeout, and the next deep-sleep transition.

Example:

```text
EoRa node boot; wake cause=4
Radio ready: 920.250 MHz, 62.5 kHz, SF8, CR 4/5, 22 dBm
Battery: 3982 mV; sequence: 12
TX attempt 1/3, 104 bytes
TX successful
Listening for downlink for 5000 ms
Downlink timeout
Entering deep sleep for 300 seconds
```

### Transmitter OLED

The SSD1306 OLED uses I²C on SDA GPIO 18 and SCL GPIO 17. It is enabled only while the node is awake and shows `SEQ`, the battery estimate, and one of these statuses: `WAKE`, `TX`, `ACK`, `CMD`, `TIMEOUT`, `RX ERROR`, `TX FAILED`, `RADIO ERROR`, or `FRAME TOO LONG`.

The final ACK/CMD/TIMEOUT/RX ERROR status remains visible for roughly one second. Before deep sleep, firmware clears the framebuffer and calls `display.setPowerSave(1)`.

> The current battery estimate is a GPIO 1 sample formula. Calibrate it to the actual battery divider before treating it as an accurate measurement.

## Gateway serial protocol

Gateway output is newline-delimited JSON only. Example uplink:

```json
{"event":"uplink","packet":{"v":1,"type":"uplink","id":"sensor-01","seq":42,"data":{"battery_mv":3982},"rx_window_ms":5000},"rssi":-87.5,"snr":8.25}
```

Queue a command from Orange Pi:

```sh
printf '%s\n' '{"action":"queue_command","id":"sensor-01","cmd":"set_interval","seconds":300}' > /dev/ttyACM0
```

Supported commands are `ping`, `set_interval` (a positive `seconds` value is required), and `sample_now`. Commands wait up to 10 minutes for the destination node's next uplink; after the ACK, the gateway sends at most one command. Orange Pi must deduplicate uplink retries by `(packet.id, packet.seq)`.

## Local gateway configuration

The gateway starts a Wi-Fi AP using `gateway_config.h`; change the default AP password before deployment. Browse `http://192.168.4.1/diagnostics` for packet count, latest node/RSSI/SNR, queue count, and the fixed profile. `POST /reboot` restarts the gateway. The firmware intentionally provides no runtime route for changing radio parameters.

## Testing

Run repository contract tests with:

```sh
python3 -m unittest discover -s tests -v
```

These tests validate source structure, a consistent radio profile, timer/deep-sleep flow, protocol, and DEBUG/OLED requirements. Physical RF testing still requires flashed boards, antennas, safe separation/attenuation, and bidirectional uplink/ACK/command verification.

## License

This project is distributed under the [GNU Lesser General Public License v2.1](LICENSE) (LGPL-2.1), as provided in the `LICENSE` file. The software is provided **without warranty**.

## Sponsor

Support this project at [esp32.vn](https://esp32.vn).
