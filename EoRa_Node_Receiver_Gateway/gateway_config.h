#pragma once

// Change before deployment. Do not use deployment credentials in source control.
constexpr char GATEWAY_AP_SSID[] = "EoRa-Gateway-Setup";
constexpr char GATEWAY_AP_PASSWORD[] = "change-me-now";  // 8+ characters
constexpr char GATEWAY_HOSTNAME[] = "eora-gateway";
constexpr uint32_t COMMAND_TTL_MS = 10UL * 60UL * 1000UL;
constexpr size_t MAX_COMMAND_QUEUE = 8;
constexpr size_t MAX_SERIAL_LINE_BYTES = 512;
