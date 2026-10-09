# 01 — Hello Device

[English](README.md) | [Tiếng Việt](README_vi.md)


Đưa một ESP32 trắng lên InnoEdge Cloud. Đây là example đầu tiên nên chạy.

## Yêu cầu
- ESP-IDF ≥ 5.1
- Bo ESP32-S3 bất kỳ (không cần màn hình, không cần relay)
- Một cloud để nối vào — **không cần đăng ký gì**, dùng cloud giả trong repo

## Chạy cloud giả trước (terminal riêng)
```bash
go run ../../tools/mock-cloud
```
Nó in ra IP LAN cần dùng ở bước sau. Xem [`tools/mock-cloud`](../../tools/mock-cloud)
để biết cách gõ lệnh điều khiển máy.

## Build & flash
```bash
idf.py set-target esp32s3
idf.py menuconfig      # InnoEdge SDK → cloud base URL → http://<IP mock in ra>:8080
idf.py flash monitor
```

## Cấu hình
| Nơi đổi | Tham số |
|---|---|
| `../sdkconfig.defaults` | `CONFIG_INNOEDGE_SERVER_BASE_URL` — địa chỉ cloud (mặc định là placeholder, PHẢI đổi) |
| `menuconfig` | InnoEdge SDK → mọi tham số hạ tầng |

## Kết quả mong đợi

Lần đầu (máy chưa có WiFi):
```
W (2100) hello: CHỜ CÀI WIFI — mở app, tìm thiết bị tên bắt đầu bằng InnoEdge-Setup
```
Ba cách cài WiFi — **không cách nào bắt buộc phải có app**:

| Cách | Làm thế nào | Dùng khi |
|---|---|---|
| **Điện thoại, không app** | Vào WiFi, nối mạng `InnoEdge-Setup-XX:XX` → mở trình duyệt `http://192.168.4.1` → chọn mạng, nhập mật khẩu | Lớp học, demo, lần đầu thử |
| **Nạp sẵn lúc build** | `menuconfig → InnoEdge SDK → Factory WiFi SSID/password` | Bàn thử nghiệm, CI, nhiều bo cùng một mạng |
| App di động (BLE) | App tìm `InnoEdge-Setup-XXXX` qua Bluetooth | Sản phẩm thật, chủ máy tự cài |

Máy mở **cả SoftAP lẫn BLE cùng lúc**, chọn đường nào cũng được. Cài xong máy tự reboot.

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
| Không thấy mạng `InnoEdge-Setup-XX:XX` | Máy ĐÃ có WiFi lưu sẵn nên không mở provisioning (xoá bằng `idf.py erase-flash`), hoặc còn đang boot |
| Vào `192.168.4.1` không lên | Điện thoại tự nhảy về 4G vì mạng này không có internet — tắt dữ liệu di động tạm thời |
| SDK dừng ngay, báo "vẫn là placeholder" | Chưa đổi `CONFIG_INNOEDGE_SERVER_BASE_URL` — đúng như thiết kế |
| `online=không` mãi | Sai địa chỉ cloud; dùng mock thì phải là **IP LAN**, không phải `127.0.0.1`, và máy tính phải cùng WiFi với ESP32 |
| `assigned=chưa` mãi | Cloud thật: chưa thêm máy trên app / sai `device_id`. Mock: tự gán sau 1s, chưa thấy thì kiểm tra WS đã nối chưa |
| Boot lặp sau OTA | Bản mới crash trước khi vào cloud → bootloader tự quay bản cũ (đúng như thiết kế) |
