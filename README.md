# InnoEdge SDK cho ESP32

SDK che đi độ phức tạp của hạ tầng IoT — nhưng **không lấy mất quyền phát triển
firmware và sản phẩm của bạn**.

Từ bo trắng đến máy online: **10 phút**.

```c
#include "innoedge.h"

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.0" };
    innoedge_init(&cfg);
    innoedge_start();
}
```

Bấy nhiêu là máy tự cài WiFi qua BLE, vào cloud, gửi heartbeat, nhận OTA, và
kết nối lại khi rớt mạng.

---

## Bắt đầu

```bash
git clone <repo> && cd innoedge-sdk-esp32/examples/01-hello-device
idf.py set-target esp32s3
idf.py menuconfig      # InnoEdge SDK → cloud base URL
idf.py flash monitor
```

Cần ESP-IDF ≥ 5.1. Không cần phần cứng ngoài devkit cho example 01–05.

## Các example

Chạy theo thứ tự — mỗi cái thêm đúng một khái niệm.

| # | Example | Học được gì | Cần phần cứng |
|---|---|---|---|
| 01 | [hello-device](examples/01-hello-device) | Provisioning WiFi, lên cloud, kích hoạt máy | devkit |
| 02 | [telemetry](examples/02-telemetry) | Gửi tiền + cảnh báo, hàng đợi chống mất mạng | devkit |
| 03 | [remote-command](examples/03-remote-command) | Nhận lệnh từ cloud, chống trùng | devkit |
| 04 | [device-config](examples/04-device-config) | Cấu hình động, cache offline | devkit |
| 05 | [ota](examples/05-ota) | Cập nhật từ xa, rollback, hoãn khi bận | devkit |
| 06 | [coin-relay](examples/06-coin-relay) | Đầu đọc xu + relay nhả tiền | đầu đọc xu, relay |
| 07 | [qr-payment](examples/07-qr-payment) | QR động, webhook xác nhận | devkit |
| 08 | [carwash](examples/08-carwash) | Phiên nhiều relay theo ngân sách thời gian | 4 relay |

Mỗi example có README riêng: đấu dây, build, log mong đợi, troubleshooting.

## API công khai

Toàn bộ SDK là **16 hàm**. Đọc [`innoedge.h`](components/innoedge/include/innoedge.h)
là đủ — không cần đọc source.

```c
// vòng đời
innoedge_init(&cfg);  innoedge_start();
innoedge_is_online();  innoedge_is_assigned();  innoedge_device_id();

// gửi lên cloud
innoedge_publish_payment(kind, count, amount_vnd);   // vào NVS trước, gửi sau
innoedge_queue_depth();
innoedge_alert(code, severity, message, active);

// nhận từ cloud
innoedge_register_command(action, handler);
innoedge_reboot_after_ack();

// QR / cấu hình / OTA
innoedge_request_qr(amount_vnd);
innoedge_config_json(buf, len, &version);  innoedge_config_reload();
innoedge_ota_check();
```

Sự kiện từ cloud đi qua `innoedge_events_t` truyền vào `innoedge_init()`.

## Ranh giới: SDK làm gì, bạn làm gì

| SDK lo | Bạn lo |
|---|---|
| Provisioning WiFi (BLE/SoftAP) | Màn hình, âm thanh, nút bấm |
| Kết nối cloud + xác thực + reconnect backoff | Motor, bơm, van, relay |
| Hàng đợi giao dịch bền qua mất điện | Giá bán, khuyến mãi, quy tắc kinh doanh |
| Chống trùng lệnh (bền qua reboot) | Luồng phục vụ khách |
| Cấu hình động + cache offline | Hiệu chỉnh cảm biến, thời gian bơm |
| OTA: tải, kiểm SHA-256, rollback | Nghiệp vụ của từng lệnh |
| Cảnh báo có latch | Khi nào phát cảnh báo |

**SDK không bao giờ chứa nghiệp vụ của một sản phẩm cụ thể.** Nếu bạn thấy mình
phải sửa file trong `components/innoedge/` để làm một tính năng sản phẩm — đó là
dấu hiệu API còn thiếu, hãy mở issue thay vì sửa SDK.

## Cấu trúc

```
innoedge-sdk-esp32/
├── components/              # SDK — HẠ TẦNG
│   ├── innoedge/            #   API công khai: include/innoedge.h + innoedge.c
│   │   └── Kconfig          #   tham số hạ tầng (không có GPIO nào)
│   ├── wifi_manager/        #   kết nối WiFi + retry
│   ├── provisioning/        #   BLE/SoftAP cài WiFi
│   ├── net/                 #   WebSocket, OTA, config, asset client
│   ├── config_store/        #   NVS: định danh, token, seq, cache cấu hình
│   ├── payment_queue/       #   hàng đợi giao dịch bền qua mất điện
│   └── command_bus/         #   dispatch + chống trùng lệnh
├── components-hw/           # driver phần cứng — KHÔNG thuộc SDK
│   ├── pulse_input/         #   đếm xung đầu đọc xu/bill (có chống dội)
│   ├── relay_control/       #   relay đa kênh + xung nhả tiền
│   └── wash_control/        #   phiên rửa xe theo ngân sách thời gian
├── examples/
│   ├── example.cmake        # đường dẫn component — sửa 1 chỗ duy nhất
│   ├── sdkconfig.defaults   # cấu hình chung mọi example
│   └── NN-*/                # mỗi example: CMakeLists + main/ + README
├── tests/run.sh             # test host, không cần ESP-IDF/phần cứng
└── docs/PROTOCOL-v1.md      # đặc tả giao thức thiết bị ↔ cloud
```

`components/` là thứ bán ra. `components-hw/` là driver mẫu để example 06/08
chạy được trên phần cứng thật — dùng thoải mái, nhưng đừng coi là API ổn định.

## Test

```bash
./tests/run.sh
```
Chạy trên máy dev, không cần ESP-IDF, không cần bo. Bao phủ registry lệnh +
chống trùng — phần mà lỗi sẽ khiến máy **nhả tiền hai lần**.

## Giao thức mở

[`docs/PROTOCOL-v1.md`](docs/PROTOCOL-v1.md) đặc tả đầy đủ giao thức thiết bị ↔
cloud. Mở công khai có chủ đích: bạn không bị khoá vào một nhà cung cấp — có
spec là tự viết được server thay thế. Nhưng đừng tự dựng lại: SDK đã xử lý retry,
dedupe, hàng đợi, rollback — những chỗ dễ mất tiền nhất.

---

## Việc còn lại (nợ kỹ thuật đã biết)

Ghi ra đây thay vì giấu:

| Việc | Vì sao chưa làm |
|---|---|
| Đổi tiền tố `gtek_*`/`CONFIG_GTEK_*` → `innoedge_*`/`CONFIG_IE_*` | Là một lần `sed` thuần + một lần flash cả fleet. Đổi sớm chỉ tạo diff to mà không thêm tính năng nào. |
| `gtek_config_lookup_combo()` (logic rửa xe) nằm trong `net/gtek_config_client.c` | Vi phạm ranh giới SDK ↔ sản phẩm — thuộc về `components-hw/wash_control/`. Firmware đang chạy thật dùng nó; dời khi có dịp flash fleet. |
| Chưa có transport ngoài WebSocket | WS là thứ đang chạy thật trên fleet. Thêm MQTT khi có khách cần, không trước. |
| Chưa chạy `idf.py build` lần nào | Máy dev thiếu Python venv của ESP-IDF. Đã verify: test host PASS, Kconfig parse OK, cmake path resolve OK, `-fsyntax-only` sạch. Chạy `install.sh` rồi build 8 example là chốt được. |
