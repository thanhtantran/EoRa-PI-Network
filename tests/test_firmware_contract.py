"""Contract tests for the EoRa node and Orange Pi gateway sketches."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
NODE = ROOT / "EoRa_Node_Transmitter"
GATEWAY = ROOT / "EoRa_Node_Receiver_Gateway"


class FirmwareContractTests(unittest.TestCase):
    def read(self, path: Path) -> str:
        return path.read_text(encoding="utf-8")

    def test_required_firmware_layout_exists(self) -> None:
        for path in (
            NODE / "EoRa_Node_Transmitter.ino",
            NODE / "radio_config.h",
            NODE / "protocol.h",
            NODE / "power_management.h",
            GATEWAY / "EoRa_Node_Receiver_Gateway.ino",
            GATEWAY / "radio_config.h",
            GATEWAY / "protocol.h",
            GATEWAY / "gateway_config.h",
        ):
            self.assertTrue(path.is_file(), path)

    def test_radio_presets_are_identical_and_vietnam_narrow(self) -> None:
        node_config = self.read(NODE / "radio_config.h")
        gateway_config = self.read(GATEWAY / "radio_config.h")
        self.assertEqual(node_config, gateway_config)
        for expected in (
            "920.250",
            "62.5",
            "LORA_SPREADING_FACTOR = 8",
            "LORA_CODING_RATE = 5",
            "LORA_TX_POWER_DBM = 22",
            "LORA_PREAMBLE_LENGTH = 16",
            "RADIOLIB_SX126X_SYNC_WORD_PRIVATE",
            "RADIO_SCLK_PIN = 5",
            "RADIO_MISO_PIN = 3",
            "RADIO_MOSI_PIN = 6",
            "RADIO_CS_PIN = 7",
            "RADIO_DIO1_PIN = 33",
            "RADIO_BUSY_PIN = 34",
            "RADIO_RST_PIN = 8",
        ):
            self.assertIn(expected, node_config)

    def test_node_uses_timer_sleep_and_bounded_downlink_window(self) -> None:
        source = self.read(NODE / "EoRa_Node_Transmitter.ino")
        self.assertIn("RTC_DATA_ATTR uint32_t sequence", source)
        self.assertIn("esp_sleep_enable_timer_wakeup", source)
        self.assertIn("esp_deep_sleep_start", source)
        self.assertIn("radio.startReceive()", source)
        self.assertNotIn("startReceiveDutyCycleAuto", source)
        self.assertNotIn("WiFiManager", source)
        self.assertNotIn("ESPAsyncWebServer", source)

    def test_gateway_is_continuous_receiver_with_queue_and_json_serial(self) -> None:
        source = self.read(GATEWAY / "EoRa_Node_Receiver_Gateway.ino")
        self.assertIn("radio.startReceive()", source)
        self.assertIn("queue_command", source)
        self.assertIn('"command_sent"', source)
        self.assertIn("deserializeJson", source)
        self.assertNotIn("esp_deep_sleep_start", source)
        self.assertNotIn("startReceiveDutyCycleAuto", source)
        self.assertNotIn("KY002S", source)

    def test_protocol_rejects_non_matching_commands(self) -> None:
        for protocol in (NODE / "protocol.h", GATEWAY / "protocol.h"):
            source = self.read(protocol)
            self.assertIn("isValidNodeId", source)
            self.assertIn("isSupportedCommand", source)
            self.assertIn("isMatchingDownlink", source)

    def test_node_uses_radiolib_and_arduinojson_7_compatible_arguments(self) -> None:
        source = self.read(NODE / "EoRa_Node_Transmitter.ino")
        self.assertIn("bool transmitUplink(String& uplink)", source)
        self.assertIn("bool readSensors(JsonObject data)", source)
        self.assertIn('packet["data"].to<JsonObject>()', source)
        self.assertNotIn("createNestedObject(\"data\")", source)

    def test_node_has_usb_debug_and_wake_only_oled_status(self) -> None:
        source = self.read(NODE / "EoRa_Node_Transmitter.ino")
        self.assertIn("#define DEBUG 1", source)
        self.assertIn("#include <U8g2lib.h>", source)
        self.assertIn("Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN)", source)
        self.assertIn("showStatus", source)
        self.assertIn('showStatus("TX")', source)
        self.assertIn('showStatus("ACK")', source)
        self.assertIn('showStatus("CMD")', source)
        self.assertIn('showStatus("TIMEOUT")', source)
        self.assertIn("display.setPowerSave(1)", source)


if __name__ == "__main__":
    unittest.main()
