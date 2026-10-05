# 04 — Device Config (chạy đúng cấu hình, kể cả offline)

[English](README.md) | [Tiếng Việt](README_vi.md)


Chủ máy đổi giá/combo trên app → máy áp dụng ngay, không cần reboot, và vẫn
đúng giá khi mất mạng.

## Luồng
```
Chủ máy sửa giá trên app
   ↓
Cloud gửi {"action":"config_updated","params":{"version":4}}
   ↓
SDK gọi handler của bạn → innoedge_config_reload()
   ↓
GET /api/device/config  →  lưu JSON thô + version vào NVS
   ↓
Máy đọc cache. Mất mạng vẫn đọc được.
```

## Build
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## Kết quả mong đợi
```
W (900)  config: chưa có cấu hình (ESP_ERR_NVS_NOT_FOUND) — dùng mặc định compile-time
I (6100) config: cấu hình sẵn sàng, version=3
I (6110) config: ── cấu hình version=3 ──
I (6120) config:   đơn giá: 1000 đ / xu
I (6130) config:   combo: Rửa cơ bản — 40000 đ
```
Đổi giá trên app → trong vài giây:
```
I (41200) innoedge: lệnh động id=91 action=config_updated
I (41800) config: cấu hình sẵn sàng, version=4
```

## Ba luật

**1. Đọc cache TRƯỚC khi lên mạng.** `dump_config()` gọi ngay sau `innoedge_init()`,
trước `innoedge_start()`. Máy phải phục vụ được từ giây đầu, không chờ WiFi.

**2. Lỗi mạng KHÔNG được xoá cache.** SDK giữ nguyên cache cũ khi fetch hỏng.
Đừng viết code kiểu "fetch lỗi → dùng giá 0".

**3. Vẫn ack "ok" khi fetch lỗi.** Cache cũ vẫn dùng được; ack "error" chỉ làm
cloud gửi lại vô ích.

## Cấu hình trông như thế nào
```jsonc
{
  "pricing": { "coinPerBillVnd": 1000, "coinPerQrVnd": 1000 },
  "combos": [ {"id":"A","name":"Rửa cơ bản","priceVnd":40000,
               "payload":{"steps":[{"device":"water","seconds":120}]}} ],
  "dynamic": { "ui_language": "vi" }
}
```
Trần **4096 byte**. Cần nhiều hơn → tách phần nặng sang một endpoint riêng, đừng
nhồi vào đây.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| `ESP_ERR_NVS_NOT_FOUND` mãi | Máy chưa gán đối tác → cloud không trả config |
| `cấu hình cache hỏng JSON` | Cấu hình vượt 4096 byte nên bị cắt cụt |
| Đổi giá trên app mà máy không đổi | Chưa đăng ký `config_updated` |
