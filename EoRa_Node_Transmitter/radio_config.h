#pragma once

#include <RadioLib.h>

// EoRa-S3-900TB internal SX1262 wiring. Do not change.
constexpr uint8_t RADIO_SCLK_PIN = 5;
constexpr uint8_t RADIO_MISO_PIN = 3;
constexpr uint8_t RADIO_MOSI_PIN = 6;
constexpr uint8_t RADIO_CS_PIN = 7;
constexpr uint8_t RADIO_DIO1_PIN = 33;
constexpr uint8_t RADIO_BUSY_PIN = 34;
constexpr uint8_t RADIO_RST_PIN = 8;

// Vietnam (Narrow) private-network preset. Reflash every peer together to change it.
constexpr float LORA_FREQUENCY_MHZ = 920.250;
constexpr float LORA_BANDWIDTH_KHZ = 62.5;
constexpr uint8_t LORA_SPREADING_FACTOR = 8;
constexpr uint8_t LORA_CODING_RATE = 5;  // RadioLib: LoRa 4/5
constexpr int8_t LORA_TX_POWER_DBM = 22;
constexpr uint16_t LORA_PREAMBLE_LENGTH = 16;
constexpr uint8_t LORA_SYNC_WORD = RADIOLIB_SX126X_SYNC_WORD_PRIVATE;

inline int beginRadio(SX1262& radio) {
  return radio.begin(
      LORA_FREQUENCY_MHZ, LORA_BANDWIDTH_KHZ, LORA_SPREADING_FACTOR,
      LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM,
      LORA_PREAMBLE_LENGTH, 0.0, false);
}
