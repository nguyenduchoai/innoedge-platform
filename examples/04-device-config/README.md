<a id="top"></a>
# 04 — Device Config: Cấu hình động & Đệm offline (Dynamic Cloud Pricing)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

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

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

Operators update prices or combo packages in the mobile app → The device adopts new settings immediately without rebooting, and remains operable at correct prices even during internet outages.

## Architecture Flow
```
Operator modifies price in app
   ↓
Cloud sends {"action":"config_updated","params":{"version":4}}
   ↓
SDK triggers your callback → innoedge_config_reload()
   ↓
GET /api/device/config  →  Saves raw JSON + version to NVS flash
   ↓
Device reads persistent cache. Works 100% offline.
```

## Build & Flash
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## Expected Output
```
W (900)  config: No cached config in NVS — falling back to compile-time defaults
I (6100) config: Configuration ready, version=3
I (6110) config: ── Current Config (v3) ──
I (6120) config:   Unit price: 1,000 VND / coin
I (6130) config:   Combo: Standard Wash — 40,000 VND
```
Update pricing on dashboard → Within a few seconds:
```
I (41200) innoedge: Dynamic command id=91 action=config_updated
I (41800) config: Configuration ready, version=4
```

## Three Essential Rules

1. **Read cache BEFORE connecting.** Call `dump_config()` immediately after `innoedge_init()`, before `innoedge_start()`. The machine must be ready to serve customers on second one without waiting for WiFi.
2. **Network errors must NEVER clear existing cache.** The SDK preserves old cached parameters when fetch fails. Never write fallback logic that resets prices to zero.
3. **Always ACK "ok" on network reload errors.** The old cache is still valid; returning an error only causes pointless retry spam.

## Configuration Schema Example
```jsonc
{
  "pricing": { "coinPerBillVnd": 1000, "coinPerQrVnd": 1000 },
  "combos": [
    {
      "id": "A",
      "name": "Standard Wash",
      "priceVnd": 40000,
      "payload": { "steps": [{ "device": "water", "seconds": 120 }] }
    }
  ],
  "dynamic": { "ui_language": "en" }
}
```
*Note:* The configuration payload has a ceiling of **4096 bytes**. Large assets (audio/video files) should be downloaded from dedicated asset endpoints.
