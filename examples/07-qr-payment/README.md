<a id="top"></a>
# 07 — QR Payment: Thanh toán quét mã QR ngân hàng (Instant Banking Payments)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

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

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

Customers scan a dynamic QR code using banking or e-wallet apps; the hardware receives a certified payment notification within ~1–3 seconds.

## Hardware Requirements
Devkit board + BOOT button. In production, connect an LCD/OLED display or e-paper screen to render the QR code.

## Architecture Flow
```
Customer selects package
   ↓  innoedge_request_qr(20000)
Cloud creates intent + calls payment gateway (VietQR, PayOS, SePAY, Tingee)
   ↓  on_qr(payload, ...)
Device displays QR code on screen — Customer scans and transfers funds
   ↓  Instant bank webhook -> Cloud
   ↓  on_paid(intent_id, amount)
Device dispenses goods / activates relay
```

## Four Essential Rules

1. **Render `payload` VERBATIM.** Never parse, alter, or reconstruct the QR string. Depending on gateway configuration, the payload may be an EMV VietQR string, a direct wallet checkout URL, or a hosted image URL. The firmware must simply render whatever string it receives.
2. **Only `on_paid` permits dispensing.** Not "customer says they transferred", not "the QR code was displayed long enough". Only a cryptographically verified webhook unlocks hardware.
3. **Handle `on_qr_error` gracefully.** If the payment gateway is down and no backup provider is configured, the cloud **intentionally refuses to issue a QR code**. Showing a QR code that cannot be verified would result in real customer financial loss. Display the error message with a "Retry" button.
4. **Dynamic QR requires internet connectivity.** Unlike cash pulses (which buffer safely in the offline NVS queue), `innoedge_request_qr()` called while offline returns `ESP_ERR_INVALID_STATE` so the screen can instruct customers to pay with cash.

## Expected Output
```
I (15200) qr: QR 20000 VND · Ref GT012D00088 · Expires in 300s · intent=88
I (15210) qr: payload: 00020101021238570010A00000072701270006970422...
I (28400) qr: PAYMENT CONFIRMED: 20000 VND (intent=88) — Starting service
```

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| `ESP_ERR_INVALID_STATE` | Device is unassigned or currently offline. |
| QR generated but `on_paid` never triggers | Gateway webhook URL is not pointing to your cloud server; verify gateway webhook configuration. |
| Banking app reports "Invalid QR code" | Firmware modified or truncated the payload instead of rendering it verbatim. |
| Frequent `on_qr_error` | Gateway API credentials expired or gateway service degraded. |
