#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <SD.h>
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
constexpr uint64_t DEFAULT_SLEEP_SECONDS = 300; // Tính bằng giây, 300s = 6p
constexpr uint32_t DOWNLINK_WINDOW_MS = 5000; // mili giây 5000 = 5s
constexpr uint32_t SERIAL_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t MINIMUM_AWAKE_MS = 20000;
constexpr uint8_t MAX_UPLINK_ATTEMPTS = 3;
constexpr uint8_t BAT_ADC_PIN = 1;
constexpr uint8_t OLED_SDA_PIN = 18;
constexpr uint8_t OLED_SCL_PIN = 17;
// Verified from the legacy EoRa firmware; validate with a physical SD card.
constexpr uint8_t SDCARD_MOSI_PIN = 11;
constexpr uint8_t SDCARD_MISO_PIN = 2;
constexpr uint8_t SDCARD_SCLK_PIN = 14;
constexpr uint8_t SDCARD_CS_PIN = 13;
constexpr char WAKE_LOG_PATH[] = "/wake_log.csv";

RTC_DATA_ATTR uint32_t sequence = 0;
RTC_DATA_ATTR uint32_t sleepSeconds = DEFAULT_SLEEP_SECONDS;

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
SPIClass sdSpi(HSPI);
uint32_t lastBatteryMv = 0;
bool sdCardReady = false;
uint16_t sdWriteSuccessCount = 0;
uint16_t sdWriteFailureCount = 0;

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

String csvEscape(const String& value) {
  String escaped = value;
  escaped.replace("\"", "\"\"");
  return "\"" + escaped + "\"";
}

void logWakeEvent(const char* event, const String& detail = "") {
  if (!sdCardReady) {
    debugf("SD write skipped: card unavailable; event=%s\n", event);
    return;
  }
  File logFile = SD.open(WAKE_LOG_PATH, FILE_APPEND);
  if (!logFile) {
    ++sdWriteFailureCount;
    debugf("SD write failed: cannot open %s; event=%s\n", WAKE_LOG_PATH, event);
    return;
  }
  const String battery = lastBatteryMv == 0 ? "" : String(lastBatteryMv);
  const size_t bytesWritten = logFile.printf("%lu,%lu,%s,%s,%s\n",
      static_cast<unsigned long>(sequence), static_cast<unsigned long>(millis()),
      csvEscape(event).c_str(), battery.c_str(), csvEscape(detail).c_str());
  logFile.close();
  if (bytesWritten == 0) {
    ++sdWriteFailureCount;
    debugf("SD write failed: zero bytes; event=%s\n", event);
    return;
  }
  ++sdWriteSuccessCount;
  debugf("SD write success: event=%s; bytes=%u\n", event, static_cast<unsigned int>(bytesWritten));
}

void logSdSummary() {
  debugf("SD log summary: ready=%s; writes_ok=%u; writes_failed=%u\n",
         sdCardReady ? "yes" : "no", sdWriteSuccessCount, sdWriteFailureCount);
}

void initSdCard() {
  debugf("SD init: HSPI MOSI=%u MISO=%u SCLK=%u CS=%u\n", SDCARD_MOSI_PIN,
         SDCARD_MISO_PIN, SDCARD_SCLK_PIN, SDCARD_CS_PIN);
  sdSpi.begin(SDCARD_SCLK_PIN, SDCARD_MISO_PIN, SDCARD_MOSI_PIN, SDCARD_CS_PIN);
  sdCardReady = SD.begin(SDCARD_CS_PIN, sdSpi);
  if (!sdCardReady) {
    debugf("SD init failed: insert a FAT32 microSD/TF card and check its contacts\n");
    return;
  }
  debugf("SD init success: card mounted\n");
  if (!SD.exists(WAKE_LOG_PATH)) {
    File logFile = SD.open(WAKE_LOG_PATH, FILE_WRITE);
    if (!logFile) {
      sdCardReady = false;
      debugf("SD init failed: cannot create %s\n", WAKE_LOG_PATH);
      return;
    }
    const size_t headerBytes = logFile.println("sequence,awake_ms,event,battery_mv,detail");
    logFile.close();
    if (headerBytes == 0) {
      sdCardReady = false;
      debugf("SD init failed: cannot write CSV header\n");
      return;
    }
    debugf("SD init success: created %s\n", WAKE_LOG_PATH);
  } else {
    debugf("SD init success: appending to %s\n", WAKE_LOG_PATH);
  }
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
      logWakeEvent("tx_success", "attempt=" + String(attempt + 1) + ";bytes=" + String(uplink.length()));
      return true;
    }
    debugf("TX failed\n");
    logWakeEvent("tx_attempt_failed", "attempt=" + String(attempt + 1));
    delay(random(100, 501));
  }
  showStatus("TX FAILED");
  logWakeEvent("tx_failed", "attempts=" + String(MAX_UPLINK_ATTEMPTS));
  return false;
}

void applyCommand(JsonDocument& packet) {
  const char* command = packet["cmd"] | "";
  if (strcmp(command, "set_interval") == 0) {
    const uint32_t seconds = packet["seconds"] | 0;
    if (seconds > 0) {
      sleepSeconds = seconds;
      logWakeEvent("command_applied", "cmd=set_interval;seconds=" + String(seconds));
    }
  }
  if (strcmp(command, "ping") == 0 || strcmp(command, "sample_now") == 0) {
    logWakeEvent("command_applied", "cmd=" + String(command));
  }
}

DownlinkStatus listenForDownlink() {
  if (radio.startReceive() != RADIOLIB_ERR_NONE) {
    logWakeEvent("downlink_rx_error", "start_receive_failed");
    return DownlinkStatus::ReceiveError;
  }
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
      logWakeEvent("downlink_command", "cmd=" + String(packet["cmd"] | ""));
      return DownlinkStatus::Command;
    }
    debugf("Matching ACK received\n");
    logWakeEvent("downlink_ack");
    return DownlinkStatus::Ack;
  }
  debugf("Downlink timeout\n");
  logWakeEvent("downlink_timeout");
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
  initSdCard();
  logWakeEvent("wake", "wake_cause=" + String(esp_sleep_get_wakeup_cause()));
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  delay(1500);  // Required board/radio power-up settling time.
  if (beginRadio(radio) != RADIOLIB_ERR_NONE) {
    debugf("Radio initialization failed\n");
    logWakeEvent("radio_init_failed");
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
  logWakeEvent("sensor_read", "battery_mv=" + String(lastBatteryMv));
  debugf("Battery: %lu mV; sequence: %lu\n", static_cast<unsigned long>(lastBatteryMv),
         static_cast<unsigned long>(sequence));
  String uplink;
  serializeJson(packet, uplink);
  if (uplink.length() > MAX_LORA_FRAME_BYTES) {
    debugf("Uplink too long: %u bytes\n", uplink.length());
    logWakeEvent("uplink_too_long", "bytes=" + String(uplink.length()));
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
  logWakeEvent("deep_sleep", "sleep_seconds=" + String(sleepSeconds));
  logSdSummary();
  holdAwakeUntilMinimum(wakeStartedAt);
  Serial.flush();
  enterDeepSleep();
}

void loop() {}
