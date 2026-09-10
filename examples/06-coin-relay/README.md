# 06 — Coin & Relay (máy coin-op hoàn chỉnh)

Tiền vào (đầu đọc xu/bill) và tiền ra (relay nhả credit) — hai chiều đầy đủ.

## Đấu dây

| Chân | Nối tới | Ghi chú |
|---|---|---|
| GPIO4 | chân COIN của đầu đọc xu | active-low, cần pull-up |
| GND | GND đầu đọc | **bắt buộc chung mass** |
| GPIO42 | chân IN của module relay | |
| 12V | nguồn đầu đọc | nguồn riêng, KHÔNG lấy từ 5V của ESP32 |

Đổi chân trong `sdkconfig.defaults` của example này (hoặc `menuconfig → InnoEdge HW drivers`, rồi `idf.py fullclean`).

> Đầu đọc xu chạy 12V, ESP32 chạy 3.3V. Nối thẳng chân xung 12V vào GPIO là
> **cháy chip**. Dùng opto-coupler hoặc module chuyển mức.

## Build
```bash
idf.py set-target esp32s3
# sửa sdkconfig.defaults nếu chân khác mặc định
idf.py flash monitor
```

Không có phần cứng vẫn build và boot được: đặt mọi GPIO `-1`, driver chỉ ghi log.

## Kết quả mong đợi
Bỏ 3 xu:
```
I (12400) coinop: khách bỏ 3 xu
I (12900) telemetry: cloud đã ghi: method=coin coins=3 amount=3000 đơn giá=1000
```
Cloud gửi `{"action":"dispense","params":{"amountVnd":20000}}`:
```
I (30100) innoedge: lệnh động id=112 action=dispense
I (30400) coinop: da nha 2 xung
```

## Ba luật của máy có tiền

**1. Firmware KHÔNG tự nhân giá.** Gửi **số xu**, để cloud quy đổi. Mỗi đối tác
một đơn giá, đổi được từ app — nhét giá vào firmware là phải flash lại cả fleet.

**2. Luôn có trần an toàn.** `GTEK_DISPENSE_MAX_PULSES` chặn một `params` sai
biến thành lệnh nhả sạch hopper.

**3. Dựa vào chống-trùng của SDK, đừng tự làm.** Cloud gửi lại `dispense` sau khi
mạng rớt là bình thường. SDK giữ high-watermark `commandId` **trong NVS** và đánh
dấu **trước** khi chạy handler → mất điện giữa lúc nhả tiền, lần gửi lại sau
reboot sẽ bị chặn. Đây là lỗi từng làm máy nhả tiền hai lần; đừng viết lại logic này.

## Debounce
Đầu đọc cơ khí nhả xung bẩn. Hai tham số phải chỉnh theo đầu đọc thật:

| Tham số | Ý nghĩa | Chỉnh khi |
|---|---|---|
| `GTEK_PULSE_MIN_MS` (35) | xung hẹp hơn = nhiễu, bỏ | đếm dư → tăng |
| `GTEK_PULSE_GAP_MS` (300) | im lặng bấy nhiêu = hết chuỗi | 5 xu thành 2 lần 2+3 → tăng |

Không có con số đúng cho mọi đầu đọc — phải đo trên máy thật.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| Đếm dư xu | Nhiễu → tăng `PULSE_MIN_MS`, kiểm tra pull-up và mass chung |
| Đếm thiếu xu | `PULSE_MIN_MS` quá lớn so với độ rộng xung thật |
| Một lượt bị tách hai giao dịch | Tăng `PULSE_GAP_MS` |
| Relay kêu mà không nhả | Nguồn relay yếu / thiếu diode flyback |
| Nhả tiền hai lần | Đang tự xử lý dedupe thay vì để SDK lo |
