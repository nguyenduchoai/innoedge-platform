# Chính sách bảo mật

InnoEdge SDK chạy trên thiết bị **nhận tiền thật**. Một lỗ hổng ở đây không chỉ
là sự cố kỹ thuật — nó là tiền của chủ máy.

## Báo lỗ hổng

**Đừng mở issue công khai.** Dùng kênh riêng tư của GitHub:

> Tab **Security** → **Report a vulnerability**
> (https://github.com/nguyenduchoai/innoedge-platform/security/advisories/new)

Kênh này riêng tư giữa bạn và maintainer, không cần email, không lộ ra ngoài
tới khi có bản vá.

Trong báo cáo, cho biết:

- Mô tả lỗ hổng và ảnh hưởng thực tế
- Các bước tái hiện (log, payload, phiên bản firmware)
- Phiên bản SDK / ESP-IDF / chip
- Cách bạn muốn được ghi nhận (hoặc muốn ẩn danh)

| Mốc | Cam kết |
|---|---|
| Xác nhận đã nhận | trong 48 giờ |
| Đánh giá ban đầu | trong 5 ngày làm việc |
| Bản vá cho lỗ hổng nghiêm trọng | trong 30 ngày |

Chúng tôi công bố sau khi có bản vá, và ghi nhận người báo trừ khi bạn muốn ẩn danh.

## Phạm vi quan tâm

Ưu tiên cao:
- Vượt qua xác thực thiết bị hoặc mạo danh thiết bị khác
- Khiến máy nhả tiền/credit sai (bỏ qua chống trùng, giả lệnh `dispense`)
- Làm mất giao dịch đã thu (phá hàng đợi bền)
- Cài được firmware không hợp lệ qua OTA, hoặc vô hiệu hoá rollback
- Đọc được token thiết bị từ NVS/log/khung tin

Ngoài phạm vi:
- Tấn công cần tháo máy và nối vào chân JTAG/UART (chống bằng Secure Boot +
  Flash Encryption ở lớp sản xuất, không phải lớp SDK)
- DoS bằng cách làm nghẽn WiFi
- Lỗ hổng của ESP-IDF hoặc thư viện bên thứ ba — báo thẳng cho upstream

## Điều bạn PHẢI làm khi đưa vào sản xuất

SDK không tự làm thay bạn những việc này:

| Việc | Vì sao |
|---|---|
| **Bật Secure Boot v2 + Flash Encryption** | Không có thì ai cầm được máy là đọc được token trong flash |
| **Mỗi máy một token riêng** | `CONFIG_INNOEDGE_FACTORY_TOKEN` chỉ để bootstrap. Dùng chung token cho cả fleet = lộ một máy là lộ tất cả |
| **Nạp token ở khâu factory provisioning, không nhúng source** | Token trong git là token đã lộ |
| **Bắt buộc TLS** (`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=n`) | Đã là mặc định — đừng tắt |
| **Ký firmware OTA** | Không ký thì bất cứ ai chiếm được kênh phân phối đều đẩy được firmware |
| **Không log credential** | SDK không log; code sản phẩm của bạn cũng đừng |

## Phiên bản được hỗ trợ

| Phiên bản | Nhận bản vá bảo mật |
|---|---|
| 0.1.x | ✅ |
