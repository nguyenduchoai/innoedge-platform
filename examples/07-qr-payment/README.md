# 07 — QR Payment (thanh toán quét mã)

Khách quét QR bằng app ngân hàng/ví, máy nhận tín hiệu tiền về trong ~1–3 giây.

## Phần cứng
Devkit + nút BOOT. Máy thật thì thêm màn hình để hiện QR.

## Luồng
```
Khách chọn gói
   ↓  innoedge_request_qr(20000)
Cloud tạo intent + gọi cổng (Pay2S/9Pay/MoMo/bank)
   ↓  on_qr(payload, ...)
Máy hiện QR — khách quét, chuyển tiền
   ↓  webhook ngân hàng → cloud
   ↓  on_paid(intent_id, amount)
Máy giao hàng / mở relay
```

## Bốn luật

**1. Render `payload` NGUYÊN VĂN.** Không parse, không dựng lại. Cùng một field
có thể là EMV VietQR, URL trang thanh toán ví, hoặc URL ảnh — tuỳ cấu hình cổng
của từng đối tác. Firmware chỉ vẽ.

**2. Chỉ `on_paid` mới được phép giao hàng.** Không phải "khách bảo đã chuyển",
không phải "QR đã hiện đủ lâu". Chỉ webhook xác nhận.

**3. `on_qr_error` phải hiện được cho khách.** Khi cổng ví lỗi và không có kênh
dự phòng, cloud **cố tình không phát QR** — phát QR mà không kênh nào xác nhận
là khách mất tiền thật. Hiện đúng `message` + nút thử lại. Cloud đã tự báo đối
tác qua alert `payment_gateway_down`, firmware không phải làm gì thêm.

**4. QR động cần online.** Khác tiền mặt (vào hàng đợi, gửi sau).
`innoedge_request_qr()` offline sẽ trả `ESP_ERR_INVALID_STATE` — hiện "vui lòng
dùng tiền mặt".

## Kết quả mong đợi
```
I (15200) qr: QR 20000đ · mã GT012D00088 · hết hạn sau 300s · intent=88
I (15210) qr: payload: 00020101021238570010A00000072701270006970422...
I (28400) qr: ĐÃ THANH TOÁN 20000đ (intent=88) — bắt đầu phục vụ
```

## Chống trả hai lần
SDK tự gửi `paid_ack` khi nhận `on_paid` để cloud thôi gửi lại. Nếu máy reboot
đúng lúc đó, cloud gửi lại — nên nghiệp vụ giao hàng nên tự chống trùng theo
`intent_id`. Với nhả tiền/credit, hãy dùng lệnh `dispense` (example 06) thay vì
làm trong `on_paid`: lệnh đó đã có chống-trùng bền qua reboot.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| `ESP_ERR_INVALID_STATE` | Máy chưa gán đối tác hoặc đang offline |
| Có `on_qr` nhưng không bao giờ `on_paid` | Webhook cổng chưa trỏ về cloud; kiểm tra cấu hình Pay2S của đối tác |
| App ngân hàng báo "mã không hợp lệ" | Firmware đang tự dựng lại payload thay vì render nguyên văn |
| `on_qr_error` liên tục | Cổng thanh toán của đối tác đang lỗi — xem alert `payment_gateway_down` trên app |
