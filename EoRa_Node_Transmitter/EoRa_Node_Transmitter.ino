#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_sleep.h>

#include "power_management.h"
#include "protocol.h"
#include "radio_config.h"

#ifndef DEBUG
#define DEBUG 0
#endif

constexpr char NODE_ID[] = "sensor-01";  // Change uniquely for each deployed node.
constexpr uint64_t DEFAULT_SLEEP_SECONDS = 300;
constexpr uint32_t DOWNLINK_WINDOW_MS = 5000;
constexpr uint8_t MAX_UPLINK_ATTEMPTS = 3;
constexpr uint8_t BAT_ADC_PIN = 1;

RTC_DATA_ATTR uint32_t sequence = 0;
RTC_DATA_ATTR uint32_t sleepSeconds = DEFAULT_SLEEP_SECONDS;

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

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

void enableSensorPower() {
  // TODO: enable only the GPIO rail required by the installed sensor.
}

void disableSensorPower() {
  // TODO: turn off the project-specific sensor rail.
}

void displayOff() {
  // TODO: power down the optional OLED after the short awake status page.
}

bool readSensors(JsonObject data) {
  const uint16_t raw = analogRead(BAT_ADC_PIN);
  if (raw == 0) {
    data["battery_error"] = "adc_read_failed";
    return false;
  }
  // TODO: calibrate this conversion for the installed battery divider.
  data["battery_mv"] = (static_cast<uint32_t>(raw) * 3300UL * 2UL) / 4095UL;
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
    if (radio.transmit(uplink) == RADIOLIB_ERR_NONE) return true;
    delay(random(100, 501));
  }
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

void listenForDownlink() {
  if (radio.startReceive() != RADIOLIB_ERR_NONE) return;
  const uint32_t deadline = millis() + DOWNLINK_WINDOW_MS;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    String frame;
    const int state = radio.receive(frame, 100);
    if (state != RADIOLIB_ERR_NONE) continue;
    JsonDocument packet;
    if (deserializeJson(packet, frame) != DeserializationError::Ok ||
        !isMatchingDownlink(packet, NODE_ID, sequence)) continue;
    if (strcmp(packet["type"] | "", "cmd") == 0) applyCommand(packet);
    return;
  }
}

void setup() {
#if DEBUG
  Serial.begin(115200);
#endif
  randomSeed(esp_random());
  disableWirelessRadios();
  enableSensorPower();
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  delay(1500);  // Required board/radio power-up settling time.
  if (beginRadio(radio) != RADIOLIB_ERR_NONE) enterDeepSleep();

  delay(random(0, 10001));  // Collision-reduction wake jitter.
  ++sequence;
  JsonDocument packet;
  packet["v"] = PROTOCOL_VERSION;
  packet["type"] = "uplink";
  packet["id"] = NODE_ID;
  packet["seq"] = sequence;
  packet["rx_window_ms"] = DOWNLINK_WINDOW_MS;
  readSensors(packet["data"].to<JsonObject>());
  String uplink;
  serializeJson(packet, uplink);
  if (uplink.length() <= MAX_LORA_FRAME_BYTES && transmitUplink(uplink)) listenForDownlink();
  enterDeepSleep();
}

void loop() {}
