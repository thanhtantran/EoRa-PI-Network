#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>

#include "gateway_config.h"
#include "protocol.h"
#include "radio_config.h"

struct QueuedCommand {
  bool active = false;
  String id;
  String command;
  uint32_t seconds = 0;
  uint32_t queuedAt = 0;
};

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
WebServer server(80);
QueuedCommand commandQueue[MAX_COMMAND_QUEUE];
String serialLine;
uint32_t packetCount = 0;
String lastNodeId;
float lastRssi = 0;
float lastSnr = 0;

void emitJson(JsonDocument& document) {
  serializeJson(document, Serial);
  Serial.print('\n');
}

void emitError(const char* code) {
  StaticJsonDocument<128> response;
  response["event"] = "error";
  response["code"] = code;
  emitJson(response);
}

size_t queuedCommandCount() {
  size_t count = 0;
  for (const auto& item : commandQueue) if (item.active) ++count;
  return count;
}

void expireCommands() {
  for (auto& item : commandQueue) {
    if (item.active && millis() - item.queuedAt > COMMAND_TTL_MS) {
      item.active = false;
      emitError("command_expired");
    }
  }
}

bool queueCommand(JsonDocument& request) {
  const char* id = request["id"] | "";
  const char* command = request["cmd"] | "";
  if (strcmp(request["action"] | "", "queue_command") != 0 || !isValidNodeId(id) ||
      !isSupportedCommand(command)) return false;
  const uint32_t seconds = request["seconds"] | 0;
  if (strcmp(command, "set_interval") == 0 && seconds == 0) return false;
  for (auto& item : commandQueue) {
    if (!item.active) {
      item.active = true;
      item.id = id;
      item.command = command;
      item.seconds = seconds;
      item.queuedAt = millis();
      return true;
    }
  }
  emitError("queue_full");
  return false;
}

void processSerialInput() {
  while (Serial.available()) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\n') {
      StaticJsonDocument<256> request;
      if (deserializeJson(request, serialLine) != DeserializationError::Ok || !queueCommand(request)) {
        emitError("invalid_command");
      }
      serialLine = "";
    } else if (character != '\r') {
      if (serialLine.length() >= MAX_SERIAL_LINE_BYTES) {
        serialLine = "";
        emitError("line_too_long");
      } else {
        serialLine += character;
      }
    }
  }
}

void sendAck(const char* id, uint32_t sequence) {
  StaticJsonDocument<128> ack;
  ack["v"] = PROTOCOL_VERSION;
  ack["type"] = "ack";
  ack["id"] = id;
  ack["seq"] = sequence;
  String frame;
  serializeJson(ack, frame);
  radio.transmit(frame);
}

void sendQueuedCommand(const char* id, uint32_t sequence) {
  for (auto& item : commandQueue) {
    if (!item.active || item.id != id) continue;
    StaticJsonDocument<192> command;
    command["v"] = PROTOCOL_VERSION;
    command["type"] = "cmd";
    command["id"] = id;
    command["seq"] = sequence;
    command["cmd"] = item.command;
    if (item.command == "set_interval") command["seconds"] = item.seconds;
    String frame;
    serializeJson(command, frame);
    if (frame.length() <= MAX_LORA_FRAME_BYTES && radio.transmit(frame) == RADIOLIB_ERR_NONE) {
      StaticJsonDocument<160> status;
      status["event"] = "command_sent";
      status["id"] = id;
      status["seq"] = sequence;
      status["cmd"] = item.command;
      emitJson(status);
      item.active = false;
    }
    return;  // At most one downlink command per uplink.
  }
}

void handleUplink() {
  String frame;
  if (radio.receive(frame, 20) != RADIOLIB_ERR_NONE) return;
  StaticJsonDocument<256> packet;
  if (deserializeJson(packet, frame) != DeserializationError::Ok ||
      packet["v"] != PROTOCOL_VERSION || strcmp(packet["type"] | "", "uplink") != 0 ||
      !isValidNodeId(packet["id"] | "") || frame.length() > MAX_LORA_FRAME_BYTES) return;

  const char* id = packet["id"];
  const uint32_t sequence = packet["seq"] | 0;
  lastNodeId = id;
  lastRssi = radio.getRSSI();
  lastSnr = radio.getSNR();
  ++packetCount;
  StaticJsonDocument<384> event;
  event["event"] = "uplink";
  event["packet"] = packet;
  event["rssi"] = lastRssi;
  event["snr"] = lastSnr;
  emitJson(event);

  sendAck(id, sequence);
  sendQueuedCommand(id, sequence);
  radio.startReceive();
}

void handleDiagnostics() {
  StaticJsonDocument<256> response;
  response["packets"] = packetCount;
  response["last_node_id"] = lastNodeId;
  response["last_rssi"] = lastRssi;
  response["last_snr"] = lastSnr;
  response["queued_commands"] = queuedCommandCount();
  response["frequency_mhz"] = LORA_FREQUENCY_MHZ;
  response["bandwidth_khz"] = LORA_BANDWIDTH_KHZ;
  String body;
  serializeJson(response, body);
  server.send(200, "application/json", body);
}

void setupConfigurationServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(GATEWAY_AP_SSID, GATEWAY_AP_PASSWORD);
  server.on("/", HTTP_GET, []() { server.send(200, "text/plain", "EoRa gateway: GET /diagnostics"); });
  server.on("/diagnostics", HTTP_GET, handleDiagnostics);
  server.on("/reboot", HTTP_POST, []() { server.send(202, "text/plain", "rebooting"); delay(50); ESP.restart(); });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  delay(1500);
  if (beginRadio(radio) != RADIOLIB_ERR_NONE) {
    emitError("radio_init_failed");
    return;
  }
  setupConfigurationServer();
  radio.startReceive();
}

void loop() {
  server.handleClient();
  processSerialInput();
  expireCommands();
  handleUplink();
}
