#pragma once

#include <WiFi.h>
#include <esp_bt.h>
#include <esp_err.h>
#include <esp_wifi.h>

inline void disableWirelessRadios() {
  WiFi.mode(WIFI_OFF);
  const esp_err_t wifiStop = esp_wifi_stop();
  if (wifiStop != ESP_OK && wifiStop != ESP_ERR_WIFI_NOT_INIT) {}
  const esp_err_t wifiDeinit = esp_wifi_deinit();
  if (wifiDeinit != ESP_OK && wifiDeinit != ESP_ERR_WIFI_NOT_INIT) {}
  const esp_err_t btDisable = esp_bt_controller_disable();
  if (btDisable != ESP_OK && btDisable != ESP_ERR_INVALID_STATE) {}
  const esp_err_t btDeinit = esp_bt_controller_deinit();
  if (btDeinit != ESP_OK && btDeinit != ESP_ERR_INVALID_STATE) {}
}
