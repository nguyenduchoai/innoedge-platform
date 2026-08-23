# 01 — Hello Device

Đưa một ESP32 trắng lên InnoEdge Cloud. Đây là example đầu tiên nên chạy.

## Yêu cầu
- ESP-IDF ≥ 5.1
- Bo ESP32-S3 bất kỳ (không cần màn hình, không cần relay)
- Một tài khoản trên InnoEdge Cloud

## Build & flash
```bash
idf.py set-target esp32s3
idf.py menuconfig      # InnoEdge SDK → đổi "InnoEdge cloud base URL" nếu cần
idf.py flash monitor
```

## Cấu hình
| Nơi đổi | Tham số |
|---|---|
| `../sdkconfig.defaults` | `CONFIG_GTEK_SERVER_BASE_URL` — địa chỉ cloud |
| `menuconfig` | InnoEdge SDK → mọi tham số hạ tầng |

## Kết quả mong đợi

Lần đầu (máy chưa có WiFi):
```
W (2100) hello: CHỜ CÀI WIFI — mở app, tìm thiết bị tên bắt đầu bằng GTEK-Setup
```
→ Mở app di động → tìm thiết bị `GTEK-Setup-XXXX` → nhập WiFi → máy tự reboot.

Sau khi có WiFi:
```
I (1200) innoedge: SDK 1.0.0 · device=AABBCCDDEEFF · fw=0.1.0 · assigned=0
I (1210) hello: device_id (MAC) = AABBCCDDEEFF — dùng mã này để thêm máy trên cloud
W (4300) hello: Máy CHƯA gán đối tác — thêm máy trong app rồi quét mã kích hoạt
I (5000) hello: online=có  assigned=chưa  hàng đợi tồn=0
```
→ Thêm máy trên app bằng `device_id` ở trên → kích hoạt:
```
I (9100) innoedge: lệnh nền tảng: activation_complete
I (9110) hello: Máy đã được gán cho đối tác — sẵn sàng phục vụ
```

Máy hiện đã online trên dashboard, gửi heartbeat mỗi 30s, và tự nhận OTA.

## Máy làm gì mà mình không phải viết
| Việc | Ai lo |
|---|---|
| Cài WiFi qua BLE/SoftAP | SDK |
| Kết nối lại khi rớt mạng (có backoff) | SDK |
| Xác thực với cloud, lưu token riêng vào NVS | SDK |
| Heartbeat (fw, RSSI, heap, lý do reboot) | SDK |
| Kiểm tra + tải + xác thực + rollback OTA | SDK |
| Đồng bộ giờ NTP | SDK |

## Troubleshooting
| Triệu chứng | Nguyên nhân thường gặp |
|---|---|
| Không thấy `GTEK-Setup-XXXX` trong app | Chưa bật `CONFIG_BT_NIMBLE_ENABLED`, hoặc máy ĐÃ có WiFi lưu sẵn (xoá bằng `idf.py erase-flash`) |
| `online=không` mãi | Sai `CONFIG_GTEK_SERVER_BASE_URL`, hoặc WiFi không ra được internet |
| `assigned=chưa` mãi | Chưa thêm máy trên app, hoặc thêm sai `device_id` |
| Boot lặp sau OTA | Bản mới crash trước khi vào cloud → bootloader tự quay bản cũ (đúng như thiết kế) |
