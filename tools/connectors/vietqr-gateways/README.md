# InnoEdge VietQR Connector: Pay2S & Tingee SDK

Dịch vụ kết nối cổng thanh toán trung gian giữa **InnoEdge Cloud / ESP32** với **Pay2S Collection Link V2** và **Tingee Node SDK (`@tingee/sdk-node`)**.

---

## 🌟 Luồng hoạt động (Workflow)

```
[ ESP32: Khách chọn gói ] 
          │
          ▼ innoedge_request_qr(amount)
[ InnoEdge Cloud / Server ]
          │
          ▼ Gọi Pay2S V2 / Tingee SDK
[ Cổng Pay2S / Tingee ] ──> [ Trả chuỗi QR / Link thanh toán ]
          │
          ▼ on_qr(payload)
[ ESP32: Hiển thị mã QR lên màn hình LCD/OLED ]
          │
          ▼ Khách quét App ngân hàng chuyển khoản
[ Ngân hàng ] ──> [ Pay2S / Tingee Webhook ]
                          │
                          ▼ POST /api/webhook/pay2s hoặc /api/webhook/tingee
                  [ InnoEdge Cloud ]
                          │
                          ▼ on_paid(intentId, amount)
                  [ ESP32: Kích Relay nhả hàng / Nhả tiền! ]
```

---

## 🚀 Cài đặt & Khởi chạy

### 1. Cài đặt Dependencies

```bash
cd tools/connectors/vietqr-gateways
npm install
```

### 2. Cấu hình Môi trường (`.env`)

Tạo file `.env` trong thư mục:

```ini
PORT=3000
INNOEDGE_CLOUD_URL=http://localhost:8080

# Cấu hình Tingee (https://developers.tingee.vn)
TINGEE_CLIENT_ID=your_tingee_client_id
TINGEE_SECRET_KEY=your_tingee_secret_key
TINGEE_ENV=production

# Cấu hình Pay2S Collection Link V2 (https://docs.pay2s.vn)
PAY2S_CLIENT_ID=your_pay2s_client_id
PAY2S_SECRET_KEY=your_pay2s_secret_key
```

### 3. Chạy dịch vụ

```bash
npm start
```

Dịch vụ sẽ mở các cổng:
* Tạo mã QR: `POST http://localhost:3000/api/qr/create`
* Webhook Tingee: `POST http://localhost:3000/api/webhook/tingee`
* Webhook Pay2S : `POST http://localhost:3000/api/webhook/pay2s`

---

## 📖 Tham khảo tài liệu chính thức
* **Pay2S Collection Link V2 SDK:** https://docs.pay2s.vn/api/collection-link-v2-sdk.html
* **Tingee Node SDK (`@tingee/sdk-node`):** https://github.com/tingeehub/tingee-node
