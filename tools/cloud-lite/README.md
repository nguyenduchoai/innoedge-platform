# InnoEdge Cloud Lite (Self-Hosted Community Edition)

Bản Cloud Server tự lưu trữ (self-hosted) dành cho cộng đồng maker, kỹ sư IoT và các doanh nghiệp nhỏ muốn vận hành hệ thống thiết bị InnoEdge độc lập mà **không bị phụ thuộc (vendor lock-in)** vào bất kỳ bên thứ ba nào.

---

## 🌟 Tính năng nổi bật

- **Tự động cấp SSL (HTTPS & WSS):** Sử dụng Caddy Proxy tự động tạo và gia hạn chứng chỉ Let's Encrypt cho tên miền của bạn.
- **Chuẩn giao thức InnoEdge Protocol v1:** Giữ kết nối WebSocket hai chiều, heartbeat, telemetry hàng đợi bền, kiểm tra phiên bản OTA.
- **Tích hợp sẵn Webhook Cổng Thanh Toán Việt Nam:**
  - **SePAY** (`POST /api/webhook/sepay`): Tự động nhận biến động số dư VietQR từ hơn 20 ngân hàng (Vietcombank, MB, Techcombank, ACB...).
  - **PayOS** (`POST /api/webhook/payos`): Cổng thanh toán VietQR mã nguồn mở chuẩn NAPAS 247.
- **Trực quan hoá Realtime Console:** Quản lý danh sách thiết bị online, gửi lệnh nhả tiền (`dispense`), test chống trùng (`dup`), cập nhật cấu hình động.
- **Tích hợp Web Bluetooth Provisioning:** Cài đặt WiFi cho mạch ngay trên trình duyệt tại `/provision/`.

---

## 🚀 Triển khai nhanh trong 5 phút

### 1. Chuẩn bị
- Một máy chủ Linux (Ubuntu 22.04 / Debian) có Docker và Docker Compose. (VPS $5/tháng trên DigitalOcean, Hetzner, Vietnix... hoặc Raspberry Pi).
- Một tên miền (VD: `iot.tenban.com`) đã trỏ bản ghi `A` về IP máy chủ.

### 2. Cài đặt

```bash
# Clone repo
git clone https://github.com/nguyenduchoai/innoedge-platform.git
cd innoedge-platform/tools/cloud-lite

# Tạo file cấu hình từ file mẫu
cp config.env.example .env

# Sửa tên miền của bạn trong .env
nano .env
# Gán DOMAIN=iot.tenban.com
```

### 3. Khởi động hệ thống

```bash
docker compose up -d --build
```

Kiểm tra trạng thái:
```bash
docker compose logs -f
```

Hệ thống sẽ tự động cấu hình SSL và mở các cổng:
- Web Console: `https://iot.tenban.com/`
- Web Provisioning: `https://iot.tenban.com/provision/`
- WebSocket thiết bị: `wss://iot.tenban.com/ws/`
- Webhook SePAY: `https://iot.tenban.com/api/webhook/sepay`
- Webhook PayOS: `https://iot.tenban.com/api/webhook/payos`
- Webhook Pay2S: `https://iot.tenban.com/api/webhook/pay2s`
- Webhook Tingee: `https://iot.tenban.com/api/webhook/tingee`

---

## ⚙️ Cấu hình thiết bị ESP32 trỏ vào Cloud Lite

Trong firmware ESP32 của bạn (`idf.py menuconfig`):
1. Vào `InnoEdge SDK` → `cloud base URL`.
2. Điền: `https://iot.tenban.com` (hoặc `http://<IP-LAN>:8080` nếu test trong mạng nội bộ).
3. Biên dịch và nạp vào mạch:
   ```bash
   idf.py flash monitor
   ```
Thiết bị sẽ tự động xuất hiện trên Web Console của bạn!

---

## 💳 Kết nối Cổng thanh toán (SePAY / PayOS / Pay2S / Tingee)

Hệ thống hỗ trợ 4 cổng thanh toán tự động phổ biến nhất tại Việt Nam. Khi khách hàng quét mã QR chuyển khoản đúng cú pháp (VD: `GTMOCKD00001`), cổng thanh toán sẽ gọi webhook về Cloud Lite và máy chủ lập tức gửi frame `payment_paid` xuống thiết bị để kích hoạt nhả hàng/nhả relay.

### 1. Cấu hình SePAY (https://sepay.vn)
1. Đăng ký tài khoản và liên kết tài khoản ngân hàng.
2. Vào mục **Cấu hình Webhook** trên SePAY Dashboard.
3. Điền Webhook URL: `https://iot.tenban.com/api/webhook/sepay` (Phương thức: `POST`).

### 2. Cấu hình PayOS (https://payos.vn)
1. Đăng ký tài khoản doanh nghiệp / cá nhân trên PayOS.
2. Vào mục **Kênh thanh toán** → **Webhook**.
3. Điền Webhook URL: `https://iot.tenban.com/api/webhook/payos` (Phương thức: `POST`).

### 3. Cấu hình Pay2S (https://pay2s.vn)
1. Đăng nhập [my.pay2s.vn](https://my.pay2s.vn/) → Vào menu **Webhook** → **Thêm webhook**.
2. Chọn ngân hàng liên kết, chọn sự kiện nhận tiền.
3. Điền Webhook URL: `https://iot.tenban.com/api/webhook/pay2s` (Phương thức: `POST`).

### 4. Cấu hình Tingee (https://developers.tingee.vn)
1. Đăng nhập cổng đối tác Tingee Merchant Portal.
2. Vào mục **Webhook** trong menu bên trái.
3. Điền Webhook URL: `https://iot.tenban.com/api/webhook/tingee` (Phương thức: `POST`).
4. Sử dụng tính năng "Kiểm tra kết nối" trên Tingee để thử nghiệm.
