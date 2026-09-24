#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "gateway_config.h"
#include "protocol.h"
#include "radio_config.h"

constexpr uint8_t OLED_SDA_PIN = 18;
constexpr uint8_t OLED_SCL_PIN = 17;
constexpr uint32_t SERIAL_CONNECT_TIMEOUT_MS = 10000;

struct QueuedCommand {
  bool active = false;
  String id;
  String command;
  uint32_t seconds = 0;
  uint32_t queuedAt = 0;
};

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
WebServer server(80);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
QueuedCommand commandQueue[MAX_COMMAND_QUEUE];
String serialLine;
uint32_t packetCount = 0;
String lastNodeId;
float lastRssi = 0;
float lastSnr = 0;
String radioStatus = "booting";

void initDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  display.begin();
  display.setPowerSave(0);
  display.setFont(u8g2_font_6x10_tf);
}

void showGatewayStatus(const String& title, const String& detail = "") {
  display.clearBuffer();
  display.drawStr(0, 12, "EoRa Gateway");
  display.drawStr(0, 30, title.c_str());
  display.drawStr(0, 46, detail.c_str());
  display.drawStr(0, 62, ("Packets: " + String(packetCount)).c_str());
  display.sendBuffer();
}

void waitForSerialConnection() {
#if ARDUINO_USB_CDC_ON_BOOT
  const uint32_t startedAt = millis();
  while (!Serial && millis() - startedAt < SERIAL_CONNECT_TIMEOUT_MS) delay(10);
#endif
}

void emitJson(JsonDocument& document) {
  serializeJson(document, Serial);
  Serial.print('\n');
  Serial.flush();
}

void emitStatus(const char* status, const String& detail = "") {
  JsonDocument response;
  response["event"] = "status";
  response["status"] = status;
  if (!detail.isEmpty()) response["detail"] = detail;
  emitJson(response);
}

void emitError(const char* code) {
  JsonDocument response;
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
      JsonDocument request;
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
  JsonDocument ack;
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
    JsonDocument command;
    command["v"] = PROTOCOL_VERSION;
    command["type"] = "cmd";
    command["id"] = id;
    command["seq"] = sequence;
    command["cmd"] = item.command;
    if (item.command == "set_interval") command["seconds"] = item.seconds;
    String frame;
    serializeJson(command, frame);
    if (frame.length() <= MAX_LORA_FRAME_BYTES && radio.transmit(frame) == RADIOLIB_ERR_NONE) {
      JsonDocument status;
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
  JsonDocument packet;
  if (deserializeJson(packet, frame) != DeserializationError::Ok ||
      packet["v"] != PROTOCOL_VERSION || strcmp(packet["type"] | "", "uplink") != 0 ||
      !isValidNodeId(packet["id"] | "") || frame.length() > MAX_LORA_FRAME_BYTES) return;

  const char* id = packet["id"];
  const uint32_t sequence = packet["seq"] | 0;
  lastNodeId = id;
  lastRssi = radio.getRSSI();
  lastSnr = radio.getSNR();
  ++packetCount;
  JsonDocument event;
  event["event"] = "uplink";
  event["packet"] = packet;
  event["rssi"] = lastRssi;
  event["snr"] = lastSnr;
  emitJson(event);
  showGatewayStatus("UPLINK", lastNodeId);

  sendAck(id, sequence);
  sendQueuedCommand(id, sequence);
  radio.startReceive();
}

void handleDashboard() {
  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EoRa Gateway</title><style>
*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#17202a;font-family:Arial,sans-serif}main{max-width:720px;margin:0 auto;padding:20px}.top{display:flex;justify-content:space-between;align-items:center;border-bottom:2px solid #159957;padding-bottom:14px}.top h1{font-size:22px;margin:0}.state{background:#d9f5e5;color:#0c6b39;padding:6px 9px;font-weight:bold;font-size:13px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:18px}.metric{background:#fff;border:1px solid #d6dce1;padding:14px;min-height:92px}.metric span{display:block;font-size:12px;color:#61707c;text-transform:uppercase}.metric strong{display:block;margin-top:8px;font-size:24px;font-weight:600;overflow-wrap:anywhere}.footer{margin-top:16px;color:#61707c;font-size:13px}.error{background:#fde3e3;color:#a12b2b}.offline{background:#e5e9ed;color:#4c5a65}@media(max-width:440px){main{padding:14px}.grid{grid-template-columns:1fr}.top h1{font-size:20px}}
</style></head><body><main><header class="top"><h1>EoRa Gateway</h1><div id="state" class="state">Loading</div></header><section class="grid"><div class="metric"><span>LoRa status</span><strong id="radio">--</strong></div><div class="metric"><span>Packets received</span><strong id="packets">0</strong></div><div class="metric"><span>Last node</span><strong id="node">No uplink yet</strong></div><div class="metric"><span>Signal RSSI</span><strong id="rssi">--</strong></div><div class="metric"><span>Signal SNR</span><strong id="snr">--</strong></div><div class="metric"><span>Queued commands</span><strong id="queue">0</strong></div></section><p class="footer">920.250 MHz · 62.5 kHz · Refreshes every 3 seconds</p></main><script>
const ids=['radio','packets','node','rssi','snr','queue'];function value(id,text){document.getElementById(id).textContent=text}function signal(value,suffix){return value===null||value===undefined?'--':Number(value).toFixed(1)+suffix}async function refresh(){const state=document.getElementById('state');try{const response=await fetch('/diagnostics');if(!response.ok)throw Error('HTTP '+response.status);const data=await response.json();value('radio',data.radio_status||'unknown');value('packets',data.packet_count??data.packets??0);value('node',data.last_node_id||'No uplink yet');value('rssi',signal(data.last_rssi,' dBm'));value('snr',signal(data.last_snr,' dB'));value('queue',data.queued_commands??0);state.textContent='Online';state.className='state'}catch(error){state.textContent='Unavailable';state.className='state error'}}</script><script>refresh();setInterval(refresh, 3000)</script></body></html>
)HTML";
  server.send_P(200, "text/html", PAGE);
}

void handleDiagnostics() {
  JsonDocument response;
  response["packets"] = packetCount;
  response["packet_count"] = packetCount;
  response["radio_status"] = radioStatus;
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
  if (!WiFi.softAP(GATEWAY_AP_SSID, GATEWAY_AP_PASSWORD)) {
    radioStatus = "wifi_error";
    emitError("wifi_ap_failed");
    showGatewayStatus("WIFI ERROR");
    return;
  }
  server.on("/", HTTP_GET, handleDashboard);
  server.on("/diagnostics", HTTP_GET, handleDiagnostics);
  server.on("/reboot", HTTP_POST, []() { server.send(202, "text/plain", "rebooting"); delay(50); ESP.restart(); });
  server.begin();
  emitStatus("wifi_ap_ready", WiFi.softAPIP().toString());
  showGatewayStatus("WIFI AP", WiFi.softAPIP().toString());
}

void setup() {
  Serial.begin(115200);
  waitForSerialConnection();
  initDisplay();
  showGatewayStatus("BOOTING");
  emitStatus("gateway_boot");
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  delay(1500);
  if (beginRadio(radio) != RADIOLIB_ERR_NONE) {
    radioStatus = "radio_init_failed";
    emitError("radio_init_failed");
    showGatewayStatus("RADIO ERROR");
    return;
  }
  emitStatus("radio_ready");
  showGatewayStatus("RADIO READY");
  setupConfigurationServer();
  if (radio.startReceive() != RADIOLIB_ERR_NONE) {
    radioStatus = "radio_receive_failed";
    emitError("radio_receive_failed");
    showGatewayStatus("RX ERROR");
    return;
  }
  emitStatus("radio_listening");
  radioStatus = "listening";
  showGatewayStatus("LISTENING", "920.250 MHz");
}

void loop() {
  server.handleClient();
  processSerialInput();
  expireCommands();
  handleUplink();
}
