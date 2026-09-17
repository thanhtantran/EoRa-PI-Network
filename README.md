# Mạng cảm biến riêng EoRa-S3-900TB

[🇻🇳 Tiếng Việt](README.md) · [🇬🇧 English](README-en.md)

Firmware cho node cảm biến EoRa-S3-900TB dùng pin và gateway EoRa-S3-900TB kết nối Orange Pi qua USB. Đây là mạng LoRa riêng theo mô hình một-đến-nhiều; không sử dụng mesh, LoRaWAN, dịch vụ đám mây hoặc Home Assistant.

## Vai trò firmware

- `EoRa_Node_Transmitter`: thức dậy bằng timer, thu thập dữ liệu, gửi uplink, nhận ACK/tối đa một lệnh trong cửa sổ ngắn, sau đó deep sleep.
- `EoRa_Node_Receiver_Gateway`: luôn hoạt động, chuyển tiếp uplink LoRa hợp lệ sang USB Serial của Orange Pi, xếp hàng lệnh từ Orange Pi, và cung cấp trang chẩn đoán Wi‑Fi cục bộ.

Chân SX1262 của EoRa-S3-900TB là cố định: SCLK 5, MISO 3, MOSI 6, CS 7, DIO1 33, BUSY 34, RST 8.

## Profile radio: Vietnam (Narrow)

Mọi thiết bị phải được nạp cùng profile cố định: **920.250 MHz, 62.5 kHz, SF8, CR 4/5, private SX126x sync word, 22 dBm, preamble 16**. Private sync word chỉ dùng để phân tách gói tin, **không phải mã hóa**. Trước khi phát RF, hãy xác nhận mức 22 dBm cùng hệ thống antenna/cáp tuân thủ giới hạn công suất dẫn và ERP áp dụng tại Việt Nam.

## Build và nạp firmware

1. Cài Arduino IDE 2.x với ESP32 Arduino Core **2.0.17** hoặc phiên bản tương thích đã được xác nhận.
2. Cài thư viện **RadioLib** và **ArduinoJson 6.x** từ Library Manager.
3. Mở tệp `.ino` trong từng thư mục sketch bằng Arduino IDE.
4. Chọn board/cổng USB phù hợp với EoRa-S3-900TB ESP32-S3 và upload.
5. Gắn antenna 50-ohm phù hợp trước khi phát.

Gateway phải luôn được cấp nguồn qua USB. Kết nối thiết bị serial USB của nó (ví dụ `/dev/ttyACM0`) ở baud rate 115200.

## Giao thức Serial

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

Gateway khởi tạo Wi‑Fi AP từ `gateway_config.h`; phải đổi mật khẩu AP mặc định trước khi triển khai. Truy cập `http://192.168.4.1/diagnostics` để xem số gói, node/RSSI/SNR gần nhất, số lệnh đang chờ và profile cố định. `POST /reboot` sẽ khởi động lại gateway. Firmware cố ý không cung cấp route thay đổi tham số radio trong runtime.

## Trạng thái kiểm thử

Chạy kiểm thử contract của repository:

```sh
python3 -m unittest discover -s tests -v
```

Các kiểm thử này xác nhận cấu trúc và các điều kiện hành vi bắt buộc. Kiểm thử RF vật lý vẫn cần hai board đã flash, antenna, khoảng cách/attenuation an toàn và xác nhận giao dịch uplink/ACK/lệnh theo cả hai chiều.
