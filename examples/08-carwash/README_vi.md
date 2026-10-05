# 08 — Car Wash (phiên nhiều thiết bị theo ngân sách thời gian)

[English](README.md) | [Tiếng Việt](README_vi.md)


Example "sản phẩm hoàn chỉnh": lệnh từ cloud + cấu hình động + nhiều relay +
an toàn phần cứng.

## Mô hình
Một combo **không phải** một chuỗi bước tự động. Nó là **ngân sách giây cho từng
thiết bị**, khách tự quyết dùng lúc nào:

```
Combo "Rửa cơ bản" = { nước: 120s, bọt: 60s, khí: 60s, hút: 0s }

Khách bấm NƯỚC  → relay nước ON, trừ ngân sách nước
Khách bấm BỌT   → relay nước OFF, relay bọt ON, trừ ngân sách bọt
Ngân sách nước = 0 → nút NƯỚC khoá, các nút khác vẫn dùng được
Mọi ngân sách = 0 (hoặc quá hạn phiên) → phiên đóng, tắt hết relay
```

Cùng lúc **chỉ một relay bật** — hai bơm chạy song song là tụt áp/hỏng bơm.

## Đấu dây

| Chân | Thiết bị |
|---|---|
| GPIO4 | relay bơm nước |
| GPIO5 | relay bơm bọt |
| GPIO6 | relay van khí |
| GPIO7 | relay máy hút |
| GPIO? | cảm biến có nước (tuỳ chọn, active-low) |

Đổi trong `menuconfig → Board — car wash`. GPIO `-1` = kênh chỉ ghi log → build
và test logic được mà không cần relay thật.

## Build
```bash
idf.py set-target esp32s3
# sửa sdkconfig.defaults nếu chân khác mặc định
idf.py flash monitor
```

## Thử
Gửi lệnh từ cloud:
```jsonc
{"type":"command","commandId":150,"action":"start_wash","params":{"combo":"A"}}
// hoặc không cần cấu hình sẵn:
{"type":"command","commandId":151,"action":"start_wash",
 "params":{"steps":[{"device":"water","seconds":120},{"device":"foam","seconds":60}]}}
```
```
I (30100) carwash: mở phiên: nước=120s bọt=60s khí=60s hút=0s
I (35100) carwash: còn: nước=120s bọt=60s khí=60s hút=0s · phiên=240s
I (50100) carwash: demo: khách bấm XỊT NƯỚC → ESP_OK
I (55100) carwash: còn: nước=115s bọt=60s khí=60s hút=0s · phiên=225s
```

## Bốn lớp an toàn

| Lớp | Chặn cái gì |
|---|---|
| Độc quyền relay | hai bơm chạy cùng lúc |
| Ngân sách từng thiết bị | khách dùng quá phần đã trả |
| Trần phiên (`MASTER_MARGIN_SEC`) | phiên treo vĩnh viễn khi khách bỏ đi |
| Watchdog `MAX_ACTIVATION_SEC` | relay kẹt ON đốt bơm |
| Cảm biến nước (tuỳ chọn) | bơm chạy khô |

Cả bốn đều nằm trong `wash_control` — chạy độc lập, không cần mạng.

## Vì sao combo đọc từ cache NVS
`gtek_config_lookup_combo()` đọc **cache**, không gọi mạng. Mất WiFi giữa ca vẫn
mở đúng phiên. Đó là lý do example 04 (config) nên chạy trước example này.

## Chỉnh theo máy thật
Đừng tin ngân sách trên giấy. Bơm thật có thời gian mồi, van thật có độ trễ,
áp nước thay đổi theo giờ. Chạy thử với xe thật rồi chỉnh `seconds` trong combo
trên app — đó là lý do combo nằm ở cloud chứ không compile vào firmware.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| `khong co combo A trong cau hinh` | Cache config chưa có / chưa gán đối tác — chạy example 04 trước |
| Relay không kêu | GPIO đang `-1`, hoặc module relay active-low mà firmware xuất active-high |
| Bấm nút không ăn | Chưa có phiên (`start_wash`), hoặc ngân sách thiết bị đó = 0 |
| Relay tắt sau đúng 600s | Watchdog `MAX_ACTIVATION_SEC` — đúng thiết kế |
| Bơm chạy khô | Lắp cảm biến nước và đặt `WASH_WATER_SENSOR_GPIO` |
