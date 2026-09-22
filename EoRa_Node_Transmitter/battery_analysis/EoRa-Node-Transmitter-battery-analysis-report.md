# Báo cáo phân tích pin thiết bị ESP32 (Deep Sleep 300s)

**Ngày phân tích:** 22/09/2026
**Nguồn dữ liệu:** `wake_log.csv` — log ghi từ thiết bị ESP32 chạy pin, chu kỳ wake mỗi 300s
**Phạm vi dữ liệu:** 794 chu kỳ wake liên tục, từ pin đầy (3958 mV) đến 3320 mV

---

## 1. Tóm tắt nhanh

| Chỉ số | Giá trị |
|---|---|
| Số chu kỳ đã ghi | 794 chu kỳ |
| Điện áp bắt đầu (pin đầy) | 3958 mV |
| Điện áp hiện tại | 3320 mV |
| Tổng điện áp đã sụt | 638 mV |
| Thời gian thực đã trôi qua | 71,5 giờ (≈ 2,98 ngày) |
| Chu kỳ thực tế trung bình | ~324 giây (300s sleep + ~24s thức) |
| tx_success / downlink_timeout | 794 / 794 (100% timeout downlink) |
| **Dự đoán tổng vòng đời pin (đến ~3000 mV)** | **≈ 96 giờ (≈ 4,0 ngày)** |
| **Số chu kỳ còn lại từ hiện tại** | **≈ 265–275 chu kỳ (≈ 1,0 ngày)** |

---

## 2. Đường cong điện áp toàn bộ log

![Đường cong điện áp toàn bộ](images/chart1_full_curve.png)

Đường cong thể hiện rõ dạng điển hình của pin Li-ion/LiPo 1 cell:

1. **Giai đoạn đầu (seq 1–300):** sụt áp nhanh, ~0,9–1,4 mV/chu kỳ — giai đoạn pin rời điện áp đỉnh sau sạc.
2. **Giai đoạn plateau (seq ~300–740):** sụt áp chậm dần, xuống thấp nhất ~0,42–0,53 mV/chu kỳ quanh seq 600–700 — vùng dung lượng giữa của pin, điện áp gần như phẳng.
3. **Giai đoạn "knee" bắt đầu (seq ~740 trở đi):** tốc độ sụt áp **tăng tốc trở lại** (0,67 → 0,86 mV/chu kỳ) — dấu hiệu pin bắt đầu vào vùng gần cạn.

---

## 3. Tốc độ sụt áp theo từng khoảng 50 chu kỳ

![Tốc độ sụt áp theo khoảng](images/chart2_windowed_rate.png)

| Khoảng seq | V đầu (mV) | V cuối (mV) | Sụt (mV) | Tốc độ (mV/chu kỳ) |
|---|---|---|---|---|
| 1–50 | 3958 | 3890 | 68 | 1,39 |
| 51–100 | 3892 | 3850 | 42 | 0,86 |
| 101–150 | 3848 | 3795 | 53 | 1,08 |
| 151–200 | 3795 | 3747 | 48 | 0,98 |
| 201–250 | 3744 | 3698 | 46 | 0,94 |
| 251–300 | 3697 | 3648 | 49 | 1,00 |
| 301–350 | 3647 | 3603 | 44 | 0,90 |
| 351–400 | 3602 | 3563 | 39 | 0,80 |
| 401–450 | 3557 | 3529 | 28 | 0,57 |
| 451–500 | 3529 | 3499 | 30 | 0,61 |
| 501–550 | 3499 | 3471 | 28 | 0,57 |
| 551–600 | 3470 | 3444 | 26 | 0,53 |
| 601–650 | 3444 | 3420 | 24 | 0,49 |
| 651–700 | 3418 | 3392 | 26 | 0,53 |
| 701–750 | 3391 | 3358 | 33 | 0,67 |
| **751–794** | **3357** | **3320** | **37** | **0,86** |

Điểm đáy tốc độ sụt áp nằm ở khoảng seq 601–650 (0,49 mV/chu kỳ). Từ đó về sau tốc độ tăng dần trở lại — đây là tín hiệu rõ ràng cho thấy pin đã bước vào đoạn dốc cuối (knee) của đường cong xả.

---

## 4. Mô hình dự đoán & vùng "knee"

![Knee extrapolation](images/chart3_knee_extrapolation.png)

Vì đã quan sát được điểm gãy thực sự (không còn nằm trong vùng plateau phẳng như log trước), một mô hình hồi quy **bậc 2 (quadratic)** khớp trên đoạn dữ liệu seq ≥ 650 cho kết quả:

```
V(seq) = -0.001172 × seq² + 1.0111 × seq + 3257.3
R² = 0.9948
```

Độ khớp R² ≈ 0,995 — rất cao, nghĩa là mô hình mô tả tốt xu hướng dữ liệu quan sát được và việc ngoại suy vài trăm chu kỳ tới là hợp lý.

---

## 5. Dự đoán tổng vòng đời pin theo các ngưỡng cutoff

![Dự đoán vòng đời](images/chart4_runtime_prediction.png)

| Ngưỡng "hết pin" | Tổng vòng đời (từ pin đầy) | Còn lại từ hiện tại (3320mV) |
|---|---|---|
| 3200 mV | ~82,5 giờ (3,44 ngày) | ~11,0 giờ (0,46 ngày) |
| 3100 mV | ~89,8 giờ (3,74 ngày) | ~18,3 giờ (0,76 ngày) |
| **3000 mV** | **~96,2 giờ (4,01 ngày)** | **~24,7 giờ (1,03 ngày)** |
| 2900 mV | ~101,9 giờ (4,25 ngày) | ~30,4 giờ (1,27 ngày) |

> Hai đường fit (fit rộng seq≥500 và fit tập trung vùng knee seq≥650) cho kết quả khá thống nhất — chênh lệch vài phần trăm — càng củng cố độ tin cậy của dự đoán.

### Kết luận dự đoán

Với ESP32 chạy trực tiếp trên pin Li-ion/LiPo 1S (không có dấu hiệu boost regulator trong log — điện áp ADC bám sát điện áp pin thực), ngưỡng an toàn phổ biến để đảm bảo module WiFi hoạt động ổn định khi transmit là **~3,0–3,2 V**.

**→ Vòng đời thực tế của pin từ lúc sạc đầy (3958 mV) đến khi thiết bị không còn hoạt động ổn định ước tính vào khoảng 3,7 – 4,0 ngày (89 – 96 giờ), tương đương ~940–1070 chu kỳ wake.**

Từ thời điểm hiện tại (3320 mV, chu kỳ 794), thiết bị còn khoảng **265–275 chu kỳ**, tương đương **~1 ngày** hoạt động nữa trước khi chạm ngưỡng 3000 mV.

---

## 6. Giới hạn & khuyến nghị

- **Độ tin cậy ở ngưỡng ≥3100mV: cao** — nằm sát vùng dữ liệu đã quan sát trực tiếp.
- **Độ tin cậy ở ngưỡng ≤3000mV: trung bình** — đường xả Li-ion thực tế thường **rơi gần như thẳng đứng** ở đoạn cuối cùng (dưới ~2,9V), trong khi mô hình bậc 2 có xu hướng làm mượt đoạn dốc này → số liệu ở ngưỡng thấp có thể **lạc quan hơn thực tế** (pin có thể cạn nhanh hơn dự đoán).
- Để có con số chính xác tuyệt đối, nên để thiết bị chạy tiếp đến khi tự brown-out/tắt hẳn và ghi lại điểm cuối cùng.
- **Quan sát phụ (không liên quan trực tiếp đến pin):** toàn bộ 794/794 chu kỳ đều bị `downlink_timeout` — thiết bị chưa từng nhận downlink ACK. Đây có thể là nguyên nhân khiến thời gian thức (awake_ms trung bình ~24s, có lúc lên tới ~29s) dài hơn cần thiết, làm tiêu hao pin nhanh hơn mức tối thiểu. Nếu khắc phục được downlink, vòng đời pin có thể kéo dài thêm.

---

*Báo cáo được tạo tự động từ phân tích log `wake_log.csv` (794 dòng chu kỳ, 3970 dòng log thô).*
