# Refactor change log

## Replaced legacy firmware roles

- Replaced the former Wi-Fi HTTP `WOR_Transmitter` with a timer-wake battery sensor node. Removed WiFiManager, Wi-Fi UDP/NTP, HTTP server, hard-coded credentials, `/relay`, and the two-packet `sendWOR()` flow because none belongs on a sleeping sensor node.
- Replaced the former sleeping relay `WOR_Receiver` with an always-powered Orange Pi gateway. Removed KY-002S control, `TRIGGER`, GPIO16/74HC04 external wake, relay countdown dispatch, Wake-on-Radio duty-cycle receive, and all gateway deep-sleep paths.

## Radio and protocol

- Replaced 915 MHz / 125 kHz / SF7 / CR 4/7 / preamble 512 settings with the fixed Vietnam Narrow preset: 920.250 MHz / 62.5 kHz / SF8 / CR 4/5 / private sync / 22 dBm / preamble 16.
- Added newline-delimited USB JSON, compact LoRa JSON uplinks, sequence-matched ACKs and commands, a bounded node downlink window, bounded retries with jitter, and command validation.
- Added an in-memory gateway command queue with one-command-per-matching-uplink behavior and ten-minute expiry.

## Power and operations

- Node explicitly disables Wi-Fi and Bluetooth, reads sensors before sleeping, preserves sequence and interval in RTC RAM, and uses internal timer wake.
- Gateway retains Wi-Fi only for a small local diagnostics/configuration AP. Radio parameters cannot be edited at runtime.
