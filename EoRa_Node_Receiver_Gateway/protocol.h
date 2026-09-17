#pragma once

#include <ArduinoJson.h>

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t MAX_LORA_FRAME_BYTES = 180;

inline bool isValidNodeId(const char* id) {
  if (id == nullptr || *id == '\0') return false;
  for (const char* cursor = id; *cursor != '\0'; ++cursor) {
    if (!((*cursor >= 'a' && *cursor <= 'z') ||
          (*cursor >= 'A' && *cursor <= 'Z') ||
          (*cursor >= '0' && *cursor <= '9') || *cursor == '-' || *cursor == '_')) return false;
  }
  return true;
}

inline bool isSupportedCommand(const char* command) {
  return command != nullptr &&
         (strcmp(command, "ping") == 0 || strcmp(command, "set_interval") == 0 ||
          strcmp(command, "sample_now") == 0);
}

inline bool isMatchingDownlink(JsonDocument& packet, const char* nodeId, uint32_t sequence) {
  const char* type = packet["type"] | "";
  const char* id = packet["id"] | "";
  if (packet["v"] != PROTOCOL_VERSION || strcmp(id, nodeId) != 0 ||
      packet["seq"] != sequence) return false;
  if (strcmp(type, "ack") == 0) return true;
  return strcmp(type, "cmd") == 0 && isSupportedCommand(packet["cmd"] | "");
}
