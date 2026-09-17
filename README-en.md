# EoRa-S3-900TB private sensor network

[🇻🇳 Tiếng Việt](README.md) · [🇬🇧 English](README-en.md)

Firmware for a battery EoRa-S3-900TB sensor node and a USB-connected EoRa-S3-900TB Orange Pi gateway. It is a private point-to-multipoint LoRa network: no mesh, LoRaWAN, cloud, or Home Assistant dependency.

## Firmware roles

- `EoRa_Node_Transmitter`: wakes by timer, collects data, sends an uplink, receives ACK/one command during a short window, and deep-sleeps.
- `EoRa_Node_Receiver_Gateway`: stays awake, bridges valid LoRa uplinks to Orange Pi USB serial, queues Orange Pi commands, and hosts a small local Wi-Fi diagnostics page.

The EoRa-S3-900TB SX1262 pins are fixed: SCLK 5, MISO 3, MOSI 6, CS 7, DIO1 33, BUSY 34, RST 8.

## Radio profile: Vietnam (Narrow)

Every peer must be flashed with this exact fixed profile: **920.250 MHz, 62.5 kHz, SF8, CR 4/5, private SX126x sync word, 22 dBm, preamble 16**. The private sync word separates packets; it is **not encryption**. Confirm 22 dBm plus the installed antenna/cable system meets applicable Vietnam conducted-power/ERP limits before RF use.

## Build and upload

1. Install Arduino IDE 2.x with ESP32 Arduino Core **2.0.17** (or a documented compatible version).
2. Install library **RadioLib** and **ArduinoJson 6.x** from Library Manager.
3. Open either directory's `.ino` file as an Arduino sketch.
4. Select the board/USB port appropriate for the EoRa-S3-900TB ESP32-S3 and upload.
5. Attach the correctly matched 50-ohm antenna before transmitting.

The gateway must remain USB-powered. Connect its USB serial device (for example `/dev/ttyACM0`) at 115200 baud.

## Serial protocol

Gateway output is newline-delimited JSON only. Example uplink:

```json
{"event":"uplink","packet":{"v":1,"type":"uplink","id":"sensor-01","seq":42,"data":{"battery_mv":3982},"rx_window_ms":5000},"rssi":-87.5,"snr":8.25}
```

Queue a command from Orange Pi:

```sh
printf '%s\n' '{"action":"queue_command","id":"sensor-01","cmd":"set_interval","seconds":300}' > /dev/ttyACM0
```

Supported commands: `ping`, `set_interval` (positive `seconds` required), and `sample_now`. Commands wait up to 10 minutes for a matching node's next uplink and then at most one is sent after the ACK. Orange Pi must deduplicate retries by `(packet.id, packet.seq)`.

## Gateway local configuration

The gateway starts an AP from `gateway_config.h`; change the default AP password before deployment. Browse `http://192.168.4.1/diagnostics` for packet count, latest node/RSSI/SNR, queue count, and fixed profile. `POST /reboot` reboots it. There is intentionally no runtime route to change radio parameters.

## Test status

Run repository contract tests with:

```sh
python3 -m unittest discover -s tests -v
```

Those tests validate layout and required behavior markers. A physical RF test still requires two flashed boards, antennas, safe separation/attenuation, and bidirectional uplink/ACK/command verification.
