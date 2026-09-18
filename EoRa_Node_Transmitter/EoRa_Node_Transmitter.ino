#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <esp_sleep.h>

#include "power_management.h"
#include "protocol.h"
#include "radio_config.h"

#ifndef DEBUG
#define DEBUG 1
#endif

constexpr char NODE_ID[] = "sensor-01";  // Change uniquely for each deployed node.
constexpr uint64_t DEFAULT_SLEEP_SECONDS = 300;
constexpr uint32_t DOWNLINK_WINDOW_MS = 5000;
constexpr uint32_t SERIAL_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t MINIMUM_AWAKE_MS = 20000;
constexpr uint8_t MAX_UPLINK_ATTEMPTS = 3;
constexpr uint8_t BAT_ADC_PIN = 1;
constexpr uint8_t OLED_SDA_PIN = 18;
constexpr uint8_t OLED_SCL_PIN = 17;

RTC_DATA_ATTR uint32_t sequence = 0;
RTC_DATA_ATTR uint32_t sleepSeconds = DEFAULT_SLEEP_SECONDS;

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
uint32_t lastBatteryMv = 0;

enum class DownlinkStatus { Ack, Command, Timeout, ReceiveError };

void debugf(const char* format, ...) {
#if DEBUG
  va_list args;
  va_start(args, format);
  Serial.vprintf(format, args);
  va_end(args);
#else
  (void)format;
#endif
}

void waitForSerialConnection() {
#if DEBUG && ARDUINO_USB_CDC_ON_BOOT
  const uint32_t deadline = millis() + SERIAL_CONNECT_TIMEOUT_MS;
  while (!Serial && static_cast<int32_t>(deadline - millis()) > 0) {
    delay(10);
  }
#endif
}

void holdAwakeUntilMinimum(uint32_t wakeStartedAt) {
  const uint32_t elapsed = millis() - wakeStartedAt;
  if (elapsed < MINIMUM_AWAKE_MS) {
    const uint32_t remaining = MINIMUM_AWAKE_MS - elapsed;
    debugf("Holding awake for %lu ms\n", static_cast<unsigned long>(remaining));
    delay(remaining);
  }
}

void enableSensorPower() {
  // TODO: enable only the GPIO rail required by the installed sensor.
}

void disableSensorPower() {
  // TODO: turn off the project-specific sensor rail.
}

void displayOff() {
  display.clearBuffer();
  display.sendBuffer();
  display.setPowerSave(1);
}

void showStatus(const char* status) {
  display.setPowerSave(0);
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.setCursor(0, 12);
  display.print("EoRa sensor node");
  display.setCursor(0, 29);
  display.printf("SEQ: %lu", static_cast<unsigned long>(sequence));
  display.setCursor(0, 44);
  if (lastBatteryMv > 0) {
    display.printf("Battery: %lu mV", static_cast<unsigned long>(lastBatteryMv));
  } else {
    display.print("Battery: unavailable");
  }
  display.setCursor(0, 61);
  display.printf("Status: %s", status);
  display.sendBuffer();
}

void initDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  display.begin();
  display.setPowerSave(0);
  showStatus("WAKE");
}

bool readSensors(JsonObject data) {
  const uint16_t raw = analogRead(BAT_ADC_PIN);
  if (raw == 0) {
    data["battery_error"] = "adc_read_failed";
    return false;
  }
  // TODO: calibrate this conversion for the installed battery divider.
  lastBatteryMv = (static_cast<uint32_t>(raw) * 3300UL * 2UL) / 4095UL;
  data["battery_mv"] = lastBatteryMv;
  return true;
}

void enterDeepSleep() {
  radio.sleep();
  displayOff();
  disableSensorPower();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleepSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

bool transmitUplink(String& uplink) {
  for (uint8_t attempt = 0; attempt < MAX_UPLINK_ATTEMPTS; ++attempt) {
    showStatus("TX");
    debugf("TX attempt %u/%u, %u bytes\n", attempt + 1, MAX_UPLINK_ATTEMPTS, uplink.length());
    if (radio.transmit(uplink) == RADIOLIB_ERR_NONE) {
      debugf("TX successful\n");
      return true;
    }
    debugf("TX failed\n");
    delay(random(100, 501));
  }
  showStatus("TX FAILED");
  return false;
}

void applyCommand(JsonDocument& packet) {
  const char* command = packet["cmd"] | "";
  if (strcmp(command, "set_interval") == 0) {
    const uint32_t seconds = packet["seconds"] | 0;
    if (seconds > 0) sleepSeconds = seconds;
  }
  // ping is acknowledged by reception; sample_now has no work while already awake.
}

DownlinkStatus listenForDownlink() {
  if (radio.startReceive() != RADIOLIB_ERR_NONE) return DownlinkStatus::ReceiveError;
  debugf("Listening for downlink for %lu ms\n", static_cast<unsigned long>(DOWNLINK_WINDOW_MS));
  const uint32_t deadline = millis() + DOWNLINK_WINDOW_MS;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    String frame;
    const int state = radio.receive(frame, 100);
    if (state != RADIOLIB_ERR_NONE) continue;
    JsonDocument packet;
    if (deserializeJson(packet, frame) != DeserializationError::Ok ||
        !isMatchingDownlink(packet, NODE_ID, sequence)) continue;
    if (strcmp(packet["type"] | "", "cmd") == 0) {
      applyCommand(packet);
      debugf("Matching command received\n");
      return DownlinkStatus::Command;
    }
    debugf("Matching ACK received\n");
    return DownlinkStatus::Ack;
  }
  debugf("Downlink timeout\n");
  return DownlinkStatus::Timeout;
}

void setup() {
  const uint32_t wakeStartedAt = millis();
#if DEBUG
  Serial.begin(115200);
  waitForSerialConnection();
  Serial.printf("\nEoRa node boot; wake cause=%d\n", esp_sleep_get_wakeup_cause());
#endif
  randomSeed(esp_random());
  disableWirelessRadios();
  enableSensorPower();
  initDisplay();
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  delay(1500);  // Required board/radio power-up settling time.
  if (beginRadio(radio) != RADIOLIB_ERR_NONE) {
    debugf("Radio initialization failed\n");
    showStatus("RADIO ERROR");
    holdAwakeUntilMinimum(wakeStartedAt);
    Serial.flush();
    enterDeepSleep();
  }
  debugf("Radio ready: 920.250 MHz, 62.5 kHz, SF8, CR 4/5, 22 dBm\n");

  delay(random(0, 10001));  // Collision-reduction wake jitter.
  ++sequence;
  JsonDocument packet;
  packet["v"] = PROTOCOL_VERSION;
  packet["type"] = "uplink";
  packet["id"] = NODE_ID;
  packet["seq"] = sequence;
  packet["rx_window_ms"] = DOWNLINK_WINDOW_MS;
  readSensors(packet["data"].to<JsonObject>());
  debugf("Battery: %lu mV; sequence: %lu\n", static_cast<unsigned long>(lastBatteryMv),
         static_cast<unsigned long>(sequence));
  String uplink;
  serializeJson(packet, uplink);
  if (uplink.length() > MAX_LORA_FRAME_BYTES) {
    debugf("Uplink too long: %u bytes\n", uplink.length());
    showStatus("FRAME TOO LONG");
  } else if (transmitUplink(uplink)) {
    switch (listenForDownlink()) {
      case DownlinkStatus::Ack: showStatus("ACK"); break;
      case DownlinkStatus::Command: showStatus("CMD"); break;
      case DownlinkStatus::Timeout: showStatus("TIMEOUT"); break;
      case DownlinkStatus::ReceiveError: showStatus("RX ERROR"); break;
    }
  }
  debugf("Entering deep sleep for %lu seconds\n", static_cast<unsigned long>(sleepSeconds));
  holdAwakeUntilMinimum(wakeStartedAt);
  Serial.flush();
  enterDeepSleep();
}

void loop() {}
