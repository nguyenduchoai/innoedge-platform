# Changelog

## 0.2.0 — 2026-10-09

Bản đầu tiên **biên dịch được**. Các bản 0.1.0–0.1.4 trên ESP Component Registry
đều lỗi biên dịch trong `innoedge.c` (enum khai trùng, `s_dev.mac` không tồn tại) —
yank cùng lúc phát hành 0.2.0.

### Phá vỡ tương thích

- Gỡ khỏi `innoedge.h`: `innoedge_blackbox_*`, `innoedge_net_*`, `innoedge_cluster_init`,
  `innoedge_crypto_sign_tx/verify_tx`. Chúng chỉ là vỏ: không có driver 4G, không có
  transport cluster, khoá TAC nằm cứng trong repo công khai.
- Kconfig `CONFIG_GTEK_*` → `CONFIG_INNOEDGE_*`. Bỏ `CONFIG_GTEK_FW_VERSION`, thay bằng
  phiên bản lấy từ `PROJECT_VER` (mô tả app), xem OTA bên dưới.
- Tiền tố BLE `GTEK-Setup` → `InnoEdge-Setup`; User-Agent `gtek-fw` → `innoedge-fw`.
- API nội bộ `gtek_*` → `ie_*` (không thuộc hợp đồng công khai).
- `innoedge_hw`: bỏ alias `gtek_*`.
- Command bus: `commandId <= 0` bị từ chối (trước đây chạy mà không chống trùng).
- SDK Linux: viết lại; agent Go `linux/agent` bị gỡ (không kết nối cloud, sinh QR giả).

### Sửa lỗi tiền

- **Lệnh lệch thứ tự bị bỏ mà vẫn ack ok.** High-watermark được thay bằng nhật ký từng
  `commandId` (64 slot, bền NVS). Lệnh mất điện giữa chừng giờ được báo
  `command_interrupted`, không ack ok, không tự chạy lại.
- **`on_paid` có thể chạy hai lần.** Cloud gửi lại `payment_paid` khi `paid_ack` rơi;
  SDK không chặn (hàm `save_last_paid_intent` có nhưng không được gọi). Giờ có
  `ie_paid_guard`: nhớ từng `intentId`, chịu được hai khách trả lệch thứ tự.
- **Hàng đợi tiền có thể kẹt vĩnh viễn.** `append` (task app) và `ack` (task WS) đua
  nhau trên head/count; thêm mutex, và tự bỏ slot hỏng kèm alert critical.

### OTA

- Bắt buộc `sha256` + `size` trong manifest; kiểm cả hai, cùng project và version ghi
  trong image trước khi boot (tài liệu cũ nói có kiểm SHA-256 nhưng code không làm).
- Một nguồn phiên bản (`PROJECT_VER`) cho hello, heartbeat, OTA. Trước đây heartbeat
  báo `cfg.fw_version` còn OTA so `CONFIG_GTEK_FW_VERSION`.
- Kiểm tra định kỳ 6 giờ (bận thì 10 phút sau), không chỉ một lần lúc boot. Lệnh
  `ota_check` chỉ đánh thức task OTA, không tải trên task WebSocket.

### Sửa sau review độc lập (trước khi phát hành)

- Lệnh lệch thứ tự **sau một lần reboot** vẫn bị bỏ: watermark `last_cmd` chỉ còn
  được nâng ngưỡng khi vượt mọi id journal biết (firmware khác đã chạy lệnh).
- Cảnh báo phát lúc boot (lệnh/tiền bị ngắt, journal hỏng) không bao giờ tới cloud:
  `ie_fault` giờ giữ cảnh báo active và gửi lại mỗi lần WebSocket kết nối.
- Intent QR bị ngắt hoặc tới muộn bị coi là "đã giao" im lặng → giờ báo
  `paid_uncertain` (critical).
- OTA chờ lệnh / `on_paid` đang chạy xong rồi mới reboot (khoá thực thi chung).
- OTA theo redirect https (GitHub Release, CDN trả 302); chặn OTA lặp khi bản vừa cài
  vẫn báo version cũ (`ota_loop`).
- Hàng đợi tiền chỉ tự bỏ slot khi slot thật sự mất/sai kích thước, không phải mọi lỗi đọc.
- Linux: hai bản file trạng thái (hỏng một bản không mất gì); mất cả hai thì seq đi
  tiếp từ đồng hồ; ghi đĩa lỗi không để khung tiền nằm lại trong RAM.
- `artifact.install`: cài lại bản đang chạy là no-op; validate lỗi chỉ xoá thứ vừa tạo.
- Jumper: `--dry-run` bundle mới chạy trong sandbox systemd (không mạng/thiết bị/IPC).

### SDK Linux (`linux/python`)

- Trước: `start()` chỉ in log, không mở kết nối; khung tiền bị bỏ khi "offline".
- Giờ: WebSocket thật + backoff, hàng đợi bền ghi nguyên tử, nhật ký lệnh và chống
  giao QR hai lần như ESP32, heartbeat kèm `telemetry`, test tích hợp với mock-cloud.
- `innoedge.artifact`: cài bản phát hành có kiểm SHA-256/size, đổi `current` nguyên tử,
  rollback, giữ quyền chạy khi giải nén zip.
- Example mới: robot cua [Jumper](../linux/python/examples/jumper/) (RK3576).

### Gỡ khỏi repo công khai

- `tools/cloud-lite`: mock-cloud đóng gói như server production, không xác thực lệnh,
  webhook không kiểm chữ ký. `tools/connectors`: thuộc phần cloud đóng.
- Mã sản phẩm G-TEK không ai gọi: `ie_asset_client` (logo màn hình), `ie_voucher_client`.

### Khác

- Example 10 khai `esp_timer` (trên IDF 5.5 không còn được kéo theo bắc cầu).
- Test host chạy được trên Linux (stub dùng `int64_t` thay `long long`).
