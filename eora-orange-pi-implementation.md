# Implementation Specification: EoRa-S3-900TB Sensor Nodes and Orange Pi Gateway

## 1. Objective

Refactor `thanhtantran/EoRa-PI-Network` into a private, battery-conscious LoRa network:

- **Node Transmitter**: a battery-powered EoRa-S3-900TB sensor node. It wakes on a timer, collects sensor data, transmits it to the gateway, listens briefly for a queued command, then returns to deep sleep.
- **Node Receiver (Gateway)**: an EoRa-S3-900TB permanently connected to Orange Pi by USB. It listens continuously for uplinks, writes validated messages to USB Serial for Orange Pi, accepts commands from USB Serial, and transmits the matching command during the transmitter node's listening window. It also runs Wi-Fi only for a local configuration interface.

This is a private point-to-multipoint LoRa network. Do not add Meshtastic, LoRaWAN, mesh routing, Home Assistant, or cloud dependencies.

## 2. Hardware and operating assumptions

- All radio boards: **EoRa-S3-900TB** with E22-900MM22S / SX1262 radio.
- Node Transmitter: battery-powered ESP32-S3 sensor board. No KY-002S and no 74HC04N are required in this timer-wake design.
- Node Receiver: USB-connected EoRa-S3-900TB. Orange Pi sees it as a bidirectional serial device (for example `/dev/ttyACM0`). It is permanently powered and must not enter deep sleep.
- The existing EoRa-S3-900TB radio pin mapping is correct and must be retained:

```cpp
RADIO_SCLK_PIN = 5
RADIO_MISO_PIN = 3
RADIO_MOSI_PIN = 6
RADIO_CS_PIN   = 7
RADIO_DIO1_PIN = 33
RADIO_BUSY_PIN = 34
RADIO_RST_PIN  = 8
```

## 3. Required radio preset: Vietnam (Narrow)

Use exactly the following parameters in **both** sketches. Keep them in one shared header so they cannot drift.

```cpp
constexpr float LORA_FREQUENCY_MHZ = 920.250;
constexpr float LORA_BANDWIDTH_KHZ = 62.5;
constexpr uint8_t LORA_SPREADING_FACTOR = 8;
constexpr uint8_t LORA_CODING_RATE = 5;       // RadioLib value 5 means LoRa 4/5
constexpr int8_t LORA_TX_POWER_DBM = 22;
constexpr uint16_t LORA_PREAMBLE_LENGTH = 16;
constexpr uint8_t LORA_SYNC_WORD = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;
```

Initialize RadioLib as follows in both roles:

```cpp
SX1262 radio = new Module(
  RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN
);

int state = radio.begin(
  LORA_FREQUENCY_MHZ,
  LORA_BANDWIDTH_KHZ,
  LORA_SPREADING_FACTOR,
  LORA_CODING_RATE,
  LORA_SYNC_WORD,
  LORA_TX_POWER_DBM,
  LORA_PREAMBLE_LENGTH,
  0.0,
  false
);
```

`512` symbols in the original project were for Wake-on-Radio duty-cycle reception. Do not retain that large preamble in the timer-wake version unless a measured reliability test proves it necessary; it needlessly increases airtime and battery consumption.

## 4. Required repository structure

Create or replace the existing sketches with these clear roles:

```text
EoRa_Node_Transmitter/
  EoRa_Node_Transmitter.ino
  radio_config.h
  protocol.h
  power_management.h

EoRa_Node_Receiver_Gateway/
  EoRa_Node_Receiver_Gateway.ino
  radio_config.h
  protocol.h
  gateway_config.h
```

Duplicating `radio_config.h` and `protocol.h` is acceptable initially if Arduino IDE sketch layout makes a common parent header inconvenient; their content must stay byte-for-byte equivalent.

Do not retain the old names as the primary firmware roles: the original `WOR_Transmitter` was a Wi-Fi HTTP controller and the original `WOR_Receiver` was a sleeping relay receiver. Those names are confusing for this project.

## 5. Common packet protocol

Use newline-delimited JSON only on USB Serial. Over LoRa, use compact JSON initially for readability and debugging. Keep each LoRa frame comfortably below 180 bytes.

### 5.1 Node uplink: sensor node to gateway

```json
{"v":1,"type":"uplink","id":"sensor-01","seq":42,"data":{"temperature_c":29.4,"battery_mv":3982},"rx_window_ms":5000}
```

Required fields:

- `v`: protocol version, initially `1`.
- `type`: `uplink`.
- `id`: stable unique node ID, configurable and no spaces.
- `seq`: monotonically increasing sequence number persisted across deep-sleep resets using `RTC_DATA_ATTR`; reset to zero only after power loss is acceptable.
- `data`: sensor-specific JSON object.
- `rx_window_ms`: how long this node will listen immediately after uplink.

### 5.2 Gateway downlink: command to node

```json
{"v":1,"type":"cmd","id":"sensor-01","seq":42,"cmd":"set_interval","seconds":300}
```

- `id` must match exactly the destination node.
- `seq` must match the uplink sequence being answered.
- Start with the commands `ping`, `set_interval`, and `sample_now` only. Ignore unknown commands safely.
- A node must never execute a command for another node, a different protocol version, or an unmatched sequence.

### 5.3 Gateway acknowledgement

The gateway should immediately send this before waiting for Orange Pi processing:

```json
{"v":1,"type":"ack","id":"sensor-01","seq":42}
```

This proves the gateway received the uplink. It does not mean Orange Pi has accepted or processed the sensor data.

### 5.4 USB Serial protocol

Gateway -> Orange Pi, one JSON object per line:

```json
{"event":"uplink","packet":{"v":1,"type":"uplink","id":"sensor-01","seq":42,"data":{"temperature_c":29.4,"battery_mv":3982},"rx_window_ms":5000},"rssi":-87.5,"snr":8.25}
```

Orange Pi -> gateway, one JSON command per line:

```json
{"action":"queue_command","id":"sensor-01","cmd":"set_interval","seconds":300}
```

Gateway -> Orange Pi command status:

```json
{"event":"command_sent","id":"sensor-01","seq":42,"cmd":"set_interval"}
```

Never log diagnostic prose to the same USB Serial stream used for machine JSON. Send debug output only behind a compile-time `DEBUG` flag, or use a separate serial port if available.

## 6. Node Transmitter requirements

### 6.1 Power policy

On every wake:

1. Start USB serial only when `DEBUG` is enabled; do not block forever in `while (!Serial)`.
2. Explicitly disable Wi-Fi and Bluetooth before sensor work:

```cpp
WiFi.mode(WIFI_OFF);
esp_wifi_stop();
esp_wifi_deinit();
esp_bt_controller_disable();
esp_bt_controller_deinit();
```

Handle already-disabled/not-initialized return values without treating them as fatal. Reuse and simplify the original `eora_disable_wifi()` and `eora_disable_bluetooth()` helpers.

3. Enable only the required sensor power rail and initialize the sensor.
4. Turn OLED power/display on only while awake. Render at most one short status page if enabled.
5. Read sensors, build the uplink, and send it.
6. Power down sensors and OLED before deep sleep.

Do not globally disable ADC before analog sensors have been read. If it is used, disable ADC only after sampling and immediately before sleep.

### 6.2 Timer wake and persistence

Use ESP32-S3 internal RTC slow clock; no external RTC module is needed for an interval such as every 300 seconds.

```cpp
constexpr uint64_t DEFAULT_SLEEP_SECONDS = 300;
RTC_DATA_ATTR uint32_t sequence = 0;
RTC_DATA_ATTR uint32_t sleepSeconds = DEFAULT_SLEEP_SECONDS;

void enterDeepSleep() {
  radio.sleep();
  displayOff();
  disableSensorPower();
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}
```

After deep sleep, firmware begins from `setup()` again. Use `esp_sleep_get_wakeup_cause()` only for logging and diagnostics; both cold boot and timer wake must proceed through the normal sample/send/listen/sleep cycle.

### 6.3 Uplink and downlink window

Implement this deterministic transaction:

1. Call `radio.transmit(uplinkJson)`.
2. If transmit fails, record a debug error and sleep. Do not remain awake indefinitely.
3. Immediately call `radio.startReceive()`; do **not** use `startReceiveDutyCycleAuto()` in this short, awake receiver window.
4. Listen for `DOWNLINK_WINDOW_MS`, default `5000` ms.
5. Read packets via DIO1 interrupt or polling. Validate JSON and accept only a matching `ack` or `cmd` for this node ID and sequence.
6. Execute a valid command, persist `set_interval` in `RTC_DATA_ATTR`, and then sleep.
7. On timeout, sleep normally.

The end-to-end Orange Pi decision and serial round trip must complete inside the declared window. The gateway should send `ack` immediately and send a queued command as soon as Orange Pi returns it. Do not use a 2-second `delay()` after an uplink.

### 6.4 Sensor abstraction

Keep sensor code separate from radio/power logic:

```cpp
bool readSensors(JsonDocument& data);
void enableSensorPower();
void disableSensorPower();
```

Provide a compile-ready default implementation reporting at least `battery_mv`; leave clear TODO markers for project-specific sensors. Do not fabricate values when a sensor read fails; report an explicit error field or omit that field.

## 7. Node Receiver (Orange Pi gateway) requirements

### 7.1 Continuous radio and serial operation

- The gateway remains powered, does not call `esp_deep_sleep_start()`, and does not use Wake-on-Radio.
- Initialize SX1262 once, call `radio.startReceive()`, and keep it in receive mode whenever not transmitting.
- On each received LoRa uplink, validate it, obtain RSSI/SNR, and write exactly one JSON line to USB Serial for Orange Pi.
- Maintain an in-memory command queue keyed by node ID. A command received from Orange Pi is queued until the next valid uplink from that same node.
- When an uplink is received, transmit `ack` immediately. Then check the queue and transmit at most one matching `cmd` within that node's `rx_window_ms`. Return to `radio.startReceive()` immediately afterward.

The initial implementation only needs one packet transaction at a time. Protect radio state so it never attempts receive and transmit concurrently.

### 7.2 Wi-Fi configuration interface

Wi-Fi belongs only on this mains-powered gateway. Implement a minimal local configuration page or captive portal for:

- gateway hostname / access-point credentials,
- node-independent display of current radio preset (read-only or locked to Vietnam Narrow),
- gateway diagnostics: packet count, last node ID, last RSSI, last SNR, queued command count,
- optional reboot and clear Wi-Fi credentials actions.

Do not expose a route that changes frequency, bandwidth, spreading factor, coding rate, sync word, or transmit power at runtime. The Vietnam Narrow preset must remain fixed in firmware unless the complete network is reflashed together.

The configuration interface must not block packet reception. Use asynchronous handling or ensure all handlers are short.

### 7.3 USB serial reliability

- Run Serial at `115200` baud.
- Frame every message with a single `\n`.
- Parse input incrementally; reject lines larger than 512 bytes.
- Validate `action`, `id`, and command fields before queuing.
- Do not execute a USB command directly without a later matching node uplink.
- Emit a JSON error/status line for malformed input, queue full, unknown node ID format, or expired command.
- Expire unserved commands after 10 minutes by default.

## 8. Reliability, contention, and security constraints

1. Assign a unique static node ID to every transmitter.
2. Add a random wake jitter of 0–10 seconds before each periodic uplink so many nodes do not collide at the exact timer boundary.
3. On no ACK, retry the uplink at most twice with a short random backoff, then sleep. Battery life and channel capacity take priority over unlimited retry.
4. Include sequence numbers in all uplinks, ACKs, and commands. Orange Pi must deduplicate repeated uplinks by `id + seq`.
5. The private SX126x sync word is only packet separation; it is not encryption. Do not claim it is security. Keep protocol design ready for an authenticated message field in a later version.
6. Measure the actual on-air time and verify 22 dBm plus the installed antenna complies with the applicable Vietnam limits, including effective radiated power. Do not silently increase power above 22 dBm.

## 9. Explicit removals from the original repository

### From the former Transmitter sketch

Remove:

- `WiFi.h`, `WiFiUdp.h`, `WiFiManager.h`, NTP, UDP keepalive, and HTTP server code;
- `WiFiManager::autoConnect()` and all hardcoded access-point credentials;
- `/relay` endpoint;
- `sendWOR()` two-packet behavior;
- hardcoded 915 MHz / 125 kHz / SF7 / CR 4/7 settings.

### From the former Receiver sketch

Remove:

- KY-002S relay control (`TRIGGER`, `KY002S_PIN`, `taskDispatcher`, countdown timer);
- 74HC04N / GPIO16 external wake dependency (`WAKE_PIN`, `esp_sleep_enable_ext0_wakeup`);
- unconditional `eora_disable_wifi()` and `eora_disable_bluetooth()`; this gateway intentionally uses Wi-Fi;
- all deep sleep calls.

Also remove the unreachable original branch:

```cpp
setupLoRa();
return;
goToSleep();
```

It is both unreachable and irrelevant to the new gateway role.

## 10. Acceptance tests

### Radio configuration

- Both sketches compile under Arduino IDE using ESP32 Arduino Core 2.0.17 or a documented compatible version and RadioLib.
- Boot logs confirm: `920.250 MHz`, `62.5 kHz`, `SF8`, `CR 4/5`, and `22 dBm`.
- A transmitter and gateway exchange a valid uplink/ACK/downlink transaction.

### Battery node

- Node sends an uplink after cold boot and after every timer wake.
- Wi-Fi and Bluetooth are disabled before transmission.
- OLED is visibly off during deep sleep.
- Node sleeps after the configurable downlink window even if no gateway exists.
- A queued `set_interval` command is received and changes the subsequent sleep interval.

### Gateway and Orange Pi

- Gateway writes exactly one valid JSON line per valid LoRa uplink to `/dev/ttyACM0`.
- Orange Pi can send a JSON `queue_command` line; gateway holds it until the destination node next uplinks, then transmits it inside that node's window.
- Gateway returns to receive mode after ACK/downlink transmission.
- Gateway Wi-Fi configuration interface remains usable without interrupting normal LoRa reception.

### Multi-node

- Test at least three distinct node IDs.
- Confirm Orange Pi receives node ID, sequence, sensor values, RSSI, and SNR.
- Confirm a command for `sensor-01` is never transmitted to or acted upon by `sensor-02`.

## 11. Deliverables requested from the coding agent

1. The two compile-ready Arduino IDE sketches and all required headers.
2. A concise `README.md` with wiring, library versions, build/upload instructions, protocol examples, and Orange Pi serial examples.
3. A `CONFIGURATION.md` identifying every user-editable constant: node ID, sleep interval, downlink window, sensor enable flags, and gateway Wi-Fi credentials/reset procedure.
4. A short change log describing every removed legacy feature and why.

Do not commit generated binaries, Wi-Fi credentials, actual node IDs tied to a deployment, or Orange Pi service credentials.
