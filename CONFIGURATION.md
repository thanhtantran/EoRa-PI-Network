# Configuration

Only edit the constants below before flashing. Do not commit real Wi-Fi credentials or node IDs tied to a deployment.

## Node transmitter

In `EoRa_Node_Transmitter.ino`:

| Constant | Default | Purpose |
|---|---:|---|
| `NODE_ID` | `sensor-01` | Unique stable node identifier; letters, digits, `-`, `_` only. |
| `DEFAULT_SLEEP_SECONDS` | `300` | Timer interval after cold boot (and after RTC state is lost). |
| `DOWNLINK_WINDOW_MS` | `5000` | Post-uplink awake listening window. |
| `MAX_UPLINK_ATTEMPTS` | `3` | Initial transmit plus at most two retries. |
| `BAT_ADC_PIN` | `1` | Battery measurement ADC input. |
| `SDCARD_MOSI_PIN` / `SDCARD_MISO_PIN` / `SDCARD_SCLK_PIN` / `SDCARD_CS_PIN` | `11` / `2` / `14` / `13` | microSD/TF HSPI mapping retained from legacy firmware; validate with a physical FAT32 card. |
| `WAKE_LOG_PATH` | `/wake_log.csv` | Append-only CSV file containing wake, TX, downlink, command, deep-sleep events, and the `battery_mv` sample. |
| `DEBUG` | `1` | Set to `0` only to disable node USB diagnostic output; when enabled it reports SD mount/write status for every wake cycle. |

`set_interval` replaces `sleepSeconds` in RTC RAM. It survives deep-sleep resets, but not full power loss. The default `readSensors()` reads only a battery estimate; calibrate its divider formula and add real sensor logic at the marked TODOs. Do not fabricate sensor readings after failure: add an explicit error field or omit that field.

## Gateway

In `EoRa_Node_Receiver_Gateway/gateway_config.h`:

| Constant | Default | Purpose |
|---|---|---|
| `GATEWAY_AP_SSID` | `EoRa-Gateway-Setup` | Local configuration AP name. |
| `GATEWAY_AP_PASSWORD` | `change-me-now` | AP password; replace before deployment. |
| `GATEWAY_HOSTNAME` | `eora-gateway` | Reserved hostname for future STA integration. |
| `COMMAND_TTL_MS` | 10 min | Queued command expiry. |
| `MAX_COMMAND_QUEUE` | 8 | Maximum queued commands. |
| `MAX_SERIAL_LINE_BYTES` | 512 | Serial input safety limit. |

### Reset Wi-Fi credentials

This initial gateway uses only a firmware-configured AP and stores no station credentials. To reset it, change the AP fields, reflash, and power-cycle. Future persistent STA credential support must keep radio reception non-blocking and must not add a runtime radio-profile editor.

## Fixed network-wide radio profile

`radio_config.h` is duplicated byte-for-byte in both sketches intentionally for Arduino IDE layout. Do not alter one copy without identically altering the other and reflashing every peer. The preset is 920.250 MHz / 62.5 kHz / SF8 / CR 4/5 / private sync word / 22 dBm / preamble 16.
