# Mạng cảm biến riêng EoRa-S3-900TB

[🇻🇳 Tiếng Việt](README.md) · [🇬🇧 English](README-en.md)

Firmware cho node cảm biến EoRa-S3-900TB dùng pin và gateway EoRa-S3-900TB kết nối Orange Pi qua USB. Đây là mạng LoRa riêng theo mô hình một-đến-nhiều; không sử dụng mesh, LoRaWAN, hay bất cứ dịch vụ đám mây nào, tất cả dữ liệu đều bảo mật.

## Vai trò firmware

- `EoRa_Node_Transmitter`: thức dậy bằng timer, thu thập dữ liệu, gửi uplink, nhận ACK/tối đa một lệnh trong cửa sổ ngắn, sau đó deep sleep.
- `EoRa_Node_Receiver_Gateway`: luôn hoạt động, chuyển tiếp uplink LoRa hợp lệ sang USB Serial của Orange Pi, xếp hàng lệnh từ Orange Pi, và cung cấp trang chẩn đoán Wi‑Fi cục bộ.

Chân SX1262 của EoRa-S3-900TB là cố định: SCLK 5, MISO 3, MOSI 6, CS 7, DIO1 33, BUSY 34, RST 8.

## Profile radio: Vietnam (Narrow)

Mọi thiết bị phải được nạp cùng profile cố định: **920.250 MHz, 62.5 kHz, SF8, CR 4/5, private SX126x sync word, 22 dBm, preamble 16**. Private sync word chỉ dùng để phân tách gói tin, **không phải mã hóa**. Nếu cần mã hóa, sẽ mã hóa theo các giao thứ mã hóa mới như SHA256

Trước khi phát RF, hãy xác nhận mức 22 dBm cùng hệ thống antenna/cáp tuân thủ giới hạn công suất dẫn và ERP áp dụng tại Việt Nam.

## Build và nạp firmware

1. Cài Arduino IDE 2.x với ESP32 Arduino Core phiên bản tương thích với board EoRa-S3-900TB ESP32-S3.
2. Cài các thư viện từ Library Manager:
   - **RadioLib** 7.x;
   - **ArduinoJson** 7.x;
   - **U8g2** cho OLED SSD1306 của cả Transmitter và Gateway.
3. Mở tệp `.ino` trong từng thư mục sketch bằng Arduino IDE.
4. Chọn board/cổng USB phù hợp với EoRa-S3-900TB ESP32-S3 và upload.
5. Gắn antenna 50-ohm phù hợp trước khi phát.

Gateway phải luôn được cấp nguồn qua USB. Kết nối thiết bị serial USB của nó (ví dụ `/dev/ttyACM0`) ở baud rate 115200. Với ESP32-S3 native USB, phải chọn **Tools → USB CDC On Boot → Enabled** trước khi build/flash; Gateway chờ tối đa 10 giây để Serial Monitor kết nối.

### Trạng thái build đã xác nhận

`EoRa_Node_Transmitter` đã được build và flash thành công trên thiết bị thực với ESP32 Arduino Core 3.3.11, RadioLib 7.7.1 và ArduinoJson 7.4.3. Gateway vẫn cần được build/flash và kiểm tra riêng trên phần cứng.

## Hoạt động của Transmitter

Mặc định node ngủ **300 giây (5 phút)**. Mỗi chu kỳ wake được giữ awake tối thiểu **20 giây**; khi Arduino IDE bật USB CDC, node có thể chờ tối đa **10 giây** để Serial Monitor kết nối. Khi timer wake, firmware chạy lại từ `setup()` và thực hiện:

1. Bật USB Serial DEBUG, tắt Wi‑Fi và Bluetooth.
2. Bật OLED, hiển thị `WAKE`.
3. Khởi tạo radio SX1262 với profile Vietnam Narrow.
4. Chờ jitter ngẫu nhiên 0–10 giây để giảm va chạm giữa nhiều node.
5. Tăng sequence lưu trong RTC RAM, đọc battery ADC GPIO 1 và tạo uplink JSON.
6. Gửi uplink, tối đa 3 lần nếu transmit lỗi.
7. Chờ downlink tối đa 5 giây: `ACK`, `CMD`, `TIMEOUT` hoặc lỗi nhận.
8. Tắt OLED, ngủ SX1262 và deep sleep. Lệnh `set_interval` hợp lệ có thể thay đổi chu kỳ sleep kế tiếp; giá trị này tồn tại qua deep sleep nhưng mất khi mất nguồn hoàn toàn.

### DEBUG qua USB Serial

`DEBUG` được bật mặc định trong `EoRa_Node_Transmitter.ino`. Mở Serial Monitor ở **115200 baud** để xem boot/wake reason, radio init, battery, sequence, trạng thái TX, ACK/CMD/timeout và thời điểm deep sleep.

Với native USB của ESP32-S3, trong Arduino IDE phải chọn **Tools → USB CDC On Boot → Enabled** trước khi build/flash. Log build trước đó dùng `CDCOnBoot=default` và `ARDUINO_USB_CDC_ON_BOOT=0`, nên `Serial` không xuất qua native USB dù `DEBUG=1`. Firmware chỉ chờ Serial Monitor tối đa 10 giây, không treo vô hạn.

Ví dụ:

```text
EoRa node boot; wake cause=4
Radio ready: 920.250 MHz, 62.5 kHz, SF8, CR 4/5, 22 dBm
Battery: 3982 mV; sequence: 12
TX attempt 1/3, 104 bytes
TX successful
Listening for downlink for 5000 ms
Downlink timeout
Entering deep sleep for 300 seconds
```

### OLED của Transmitter

OLED SSD1306 sử dụng I²C với SDA GPIO 18 và SCL GPIO 17. OLED chỉ bật trong lúc node thức, hiển thị `SEQ`, battery estimate và trạng thái: `WAKE`, `TX`, `ACK`, `CMD`, `TIMEOUT`, `RX ERROR`, `TX FAILED`, `RADIO ERROR` hoặc `FRAME TOO LONG`.

Trạng thái cuối ACK/CMD/TIMEOUT/RX ERROR được giữ khoảng một giây để quan sát. Trước deep sleep, firmware xóa framebuffer và gọi `display.setPowerSave(1)`.

> Battery estimate hiện dùng công thức mẫu cho GPIO 1. Cần hiệu chuẩn theo mạch chia áp/battery thực tế trước khi dùng giá trị này làm số liệu đo chính xác.

## Nhật ký microSD của Transmitter

Transmitter hỗ trợ ghi nhật ký chu kỳ wake vào `/wake_log.csv` trên thẻ **microSD/TF định dạng FAT32**. Mapping được lấy từ firmware legacy và cần được kiểm tra với thẻ vật lý: MOSI GPIO 11, MISO GPIO 2, SCLK GPIO 14, CS GPIO 13. Node dùng `HSPI` riêng cho thẻ, không dùng chung SPI của SX1262.

Nếu không có thẻ, thẻ chưa được format, mount lỗi hoặc không tạo được file, firmware ghi `SD card initialization failed`/lỗi tương ứng qua DEBUG và vẫn tiếp tục chu trình LoRa; nó không deep sleep sớm chỉ vì SD lỗi.

Mỗi row gồm:

```text
sequence,awake_ms,event,battery_mv,detail
```

Các event được ghi gồm: `wake`, `radio_init_failed`, `sensor_read`, `tx_success`, `tx_attempt_failed`, `tx_failed`, `downlink_ack`, `downlink_command`, `downlink_timeout`, `downlink_rx_error`, `command_applied`, `uplink_too_long`, và `deep_sleep`.

Ví dụ:

```text
12,0,"wake","","wake_cause=4"
12,8421,"sensor_read","3982","battery_mv=3982"
12,9015,"tx_success","3982","attempt=1;bytes=104"
12,14037,"downlink_timeout","3982",""
12,20002,"deep_sleep","3982","sleep_seconds=300"
```

`awake_ms` là thời gian kể từ boot/wake hiện tại, không phải thời gian lịch thực: node không đồng bộ NTP và không bật Wi‑Fi để lấy thời gian. `battery_mv` chỉ là điện áp battery được đọc tại thời điểm event; cần hiệu chuẩn công thức ADC theo mạch chia áp/battery thực tế trước khi dùng làm số liệu chính xác.

Mỗi chu kỳ, DEBUG sẽ in `SD init success` hoặc `SD init failed`, `SD write success`/`SD write failed` theo từng event, sau đó `SD log summary` trước deep sleep. Dùng các dòng này để xác nhận thẻ thực sự đã mount và ghi được log.

## Gateway khởi động và hiển thị

Gateway dùng OLED SSD1306 I²C trên SDA GPIO 18, SCL GPIO 17. Sau flash/reboot, OLED phải lần lượt hiện `BOOTING`, `RADIO READY`, `WIFI AP`, sau cùng là `LISTENING` ở profile 920.250 MHz. Khi nhận uplink hợp lệ, màn hình hiện `UPLINK`, node ID và tổng số packet.

Serial Gateway vẫn là JSON Lines để Orange Pi có thể parse. Ngay sau boot mong đợi các event: `gateway_boot`, `radio_ready`, `wifi_ap_ready` (kèm IP AP) và `radio_listening`. Các lỗi tương ứng gồm `radio_init_failed`, `radio_receive_failed`, hoặc `wifi_ap_failed`.

## Giao thức Serial Gateway

Gateway chỉ xuất JSON được phân tách theo dòng mới. Ví dụ uplink:

```json
{"event":"uplink","packet":{"v":1,"type":"uplink","id":"sensor-01","seq":42,"data":{"battery_mv":3982},"rx_window_ms":5000},"rssi":-87.5,"snr":8.25}
```

Đưa lệnh vào hàng đợi từ Orange Pi:

```sh
printf '%s\n' '{"action":"queue_command","id":"sensor-01","cmd":"set_interval","seconds":300}' > /dev/ttyACM0
```

Các lệnh được hỗ trợ: `ping`, `set_interval` (bắt buộc `seconds` dương) và `sample_now`. Lệnh sẽ chờ tối đa 10 phút cho đến uplink tiếp theo của node phù hợp; sau ACK, gateway chỉ gửi tối đa một lệnh. Orange Pi phải loại trùng uplink retry theo `(packet.id, packet.seq)`.

## Cấu hình gateway cục bộ

Gateway khởi tạo Wi‑Fi AP từ `gateway_config.h`; phải đổi mật khẩu AP mặc định trước khi triển khai. Truy cập `http://192.168.4.1/` để xem dashboard chẩn đoán tự làm mới mỗi 3 giây: trạng thái LoRa, số packet, node gần nhất, RSSI, SNR và số lệnh đang chờ. `GET http://192.168.4.1/diagnostics` vẫn trả JSON cho công cụ tự động. `POST /reboot` sẽ khởi động lại gateway. Firmware cố ý không cung cấp route thay đổi tham số radio trong runtime.

## Kiểm thử

Chạy kiểm thử contract của repository:

```sh
python3 -m unittest discover -s tests -v
```

Các kiểm thử xác nhận cấu trúc, profile radio thống nhất, luồng timer/deep sleep, giao thức và yêu cầu DEBUG/OLED trong source. Kiểm thử RF vật lý vẫn cần các board đã flash, antenna, khoảng cách/attenuation an toàn và xác nhận giao dịch uplink/ACK/lệnh theo cả hai chiều.

## License

Dự án được phân phối theo [GNU Lesser General Public License v2.1](LICENSE) (LGPL-2.1), theo nội dung trong file `LICENSE`. Phần mềm được cung cấp **không kèm bảo hành**.

## Tài trợ dự án

Hỗ trợ dự án tại [esp32.vn](https://esp32.vn).
