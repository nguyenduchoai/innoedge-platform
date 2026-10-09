# InnoEdge Device Protocol v1

Đặc tả giao thức giữa thiết bị và InnoEdge Cloud.

Tài liệu này **mở công khai có chủ đích**: có spec thì bạn tự viết được server
thay thế nếu cần. Nhưng để chạy sản phẩm thật, hãy dùng SDK — nó đã xử lý retry,
dedupe, hàng đợi bền, rollback: đúng những chỗ tự làm là mất tiền.

Trạng thái: **v1 — ổn định, tương thích ngược.**

---

## 1. Kết nối

```
WebSocket  wss://<cloud-host>/ws/
Headers    Device-Id: <MAC không dấu phân cách, HOA>
           Authorization: Bearer <device_token>
```

`device_token` ban đầu là factory token; sau khi máy được gán cho một đối tác,
cloud cấp token riêng qua `activation_complete` — thiết bị lưu vào NVS và dùng
từ đó về sau.

Handshake:
```jsonc
// device → cloud
{"type":"hello","transport":"websocket","firmware":"1.2.17","local_ip":"192.168.1.20"}
// cloud → device
{"type":"hello","session_id":"..."}
// cloud → device, nếu máy chưa gán đối tác
{"type":"status","state":"unassigned"}
```

Reconnect dùng backoff, không retry liên tục:
`1s → 2s → 4s → 8s → 16s → 30s → 30s…`

## 2. Heartbeat (device → cloud)

Mặc định mỗi 30 giây.

```jsonc
{"type":"heartbeat","fw_version":"1.2.17","uptime_sec":86400,"rssi":-62,
 "heap_internal_free":142000,"heap_internal_min":98000,
 "queue_depth":0,"reset_reason":1,"last_error":""}
```

Thiết bị Linux gửi `"platform":"linux"` và có thể kèm `"telemetry":{...}` —
object tuỳ ứng dụng (nhiệt độ, trạng thái dịch vụ…). Trường **thêm, tuỳ chọn**:
cloud không biết thì bỏ qua.

`reset_reason` theo `esp_reset_reason()`: `4`=panic, `5/6/7`=watchdog,
`9`=brownout. Đây thường là manh mối đầu tiên khi máy ngoài hiện trường hay
tự reboot.

## 3. Tiền (device → cloud)

```jsonc
{"type":"coin","coins":3,"seq":1042,"ts":1765432100}                       // xu vào
{"type":"payment","method":"cash","amount":20000,"seq":1043,"ts":...}      // tiền mặt
{"type":"ticket","tickets":25,"seq":1044}                                  // vé hủy
{"type":"payment","method":"coin_out","coins":10,"seq":1045}               // xu trả ra
// cloud → device
{"type":"coin_ack","seq":1042,"status":"ok"}
```

**Luật:**
- `seq` là bộ đếm đơn điệu per-device, giữ trong NVS. Cloud dedupe theo
  `(device_id, seq)` → gửi lại bao nhiêu lần cũng không ghi trùng.
- Thiết bị gửi **số xu**, không phải tiền. Cloud quy đổi theo đơn giá của từng
  đối tác. Đừng nhúng giá vào firmware.
- `ts` là epoch giây; chưa có NTP thì để `0`, cloud lấy giờ nhận.
- **Không ack = chưa ghi được.** Giữ trong hàng đợi và gửi lại. Cloud cố tình
  không ack khi máy chưa gán đối tác hoặc ghi DB lỗi.
- `ticket` và `coin_out` **không tính doanh thu** — chúng để đối soát.

## 3b. Sự kiện tuỳ ý (device → cloud) — v1.1

Cho những gì không phải tiền và không phải sự cố: nút bấm, lựa chọn của người
dùng, số đo cảm biến theo yêu cầu.

```jsonc
{"type":"event","name":"quiz_answer","seq":1046,"ts":1765432100,"data":{"index":2}}
// cloud → device
{"type":"event_ack","seq":1046}
```

- Cùng hàng đợi bền và cùng `seq` với khung tiền — mất mạng vẫn giữ, gửi lại tới
  khi có `event_ack`. Cloud dedupe theo `(device_id, seq)`.
- `name` là định danh máy-đọc do application đặt; `data` là object tuỳ ý, ~120 byte.
- **Thêm ở v1.1** — tương thích ngược: cloud v1.0 chưa biết `event` sẽ bỏ qua,
  thiết bị giữ trong hàng đợi. Cloud phải cập nhật trước khi thiết bị dùng API này.

## 4. Cảnh báo (device → cloud)

```jsonc
{"type":"alert","code":"coin_empty","severity":"critical","message":"Het xu trong hopper"}
{"type":"alert","code":"coin_empty","active":false}   // đã hết lỗi
```

`severity`: `info` | `warning` | `error` | `critical` (mặc định `warning`).

Cloud gom theo `(device, code)`: một cảnh báo active mỗi code. Bật mới → tạo +
báo đối tác; lặp khi đang active → tăng `count`; `active:false` → resolved.

Offline: thiết bị hiện tại chỗ + ghi log, gửi khi online lại.

## 5. Lệnh động (cloud → device)

```jsonc
// cloud → device
{"type":"command","commandId":91,"action":"dispense","params":{"amountVnd":20000}}
// device → cloud
{"type":"command_ack","commandId":91,"status":"ok",
 "message":"da nha 2 xung","result":{"pulses":2}}
```

`status` là `"ok"` hoặc `"error"`. `message` là câu người-đọc; `result` là object
JSON tuỳ nghiệp vụ.

**Chống trùng — đọc kỹ nếu lệnh của bạn có tiền.**

Thiết bị giữ **nhật ký lệnh** bền trong NVS: trạng thái của từng `commandId` trong
64 lệnh gần nhất. Lệnh đã gặp **không bao giờ chạy lại**; ack phụ thuộc trạng thái:

| Trạng thái đã ghi | Ack gửi lại | Nghĩa |
|---|---|---|
| chạy xong, thành công | `{"status":"ok","message":"duplicate"}` | đã làm, đừng gửi nữa |
| chạy xong, handler lỗi | `{"status":"error","message":"previous execution failed; reconcile manually"}` | đã thử, thất bại |
| bị ngắt giữa chừng (mất điện) hoặc quá cũ | `{"status":"error","message":"execution uncertain; reconcile manually, do not replay"}` | **không biết** đã nhả hay chưa |

Thiết bị ghi "đang chạy" **trước** khi chạm phần cứng và ghi kết quả **trước** khi
ack. Còn "đang chạy" sau reboot = không chắc → báo đối soát + alert
`command_interrupted`, không tự chạy lại. Cloud **không** được tự gửi lại lệnh nhận
ack `error` kiểu này — đẩy cho người vận hành đối soát.

Lệnh tới lệch thứ tự (id 11 trước id 10) vẫn được chạy đủ — khác bản high-watermark
cũ, vốn bỏ id 10 mà vẫn ack `ok`. `commandId` phải > 0; không có id = không chạy.

**Lệnh nền tảng** (SDK tự xử lý, không qua registry):
`activation_complete` · `reboot` · `ota_check` · `ble_provision` · `set_display`

## 6. QR động

```jsonc
// device → cloud
{"type":"qr_request","amount":20000,"seq":1044}
// cloud → device
{"type":"qr","seq":1044,"intentId":88,"refCode":"GT012D00088","amount":20000,
 "qrPayload":"000201...6304ABCD","payloadFormat":"emv","expiresSec":300}
// cloud → device, khi cổng lỗi và không có kênh dự phòng
{"type":"qr_error","seq":1044,"message":"Cong thanh toan gian doan, vui long thu lai"}
// cloud → device, sau khi webhook ngân hàng/ví xác nhận
{"type":"payment_paid","intentId":88,"amount":20000,"refCode":"GT012D00088"}
// device → cloud
{"type":"paid_ack","intentId":88}
```

`payloadFormat` cho biết **nguồn gốc** chuỗi, không phải cấu trúc để parse:

| Giá trị | Nghĩa | Khách quét bằng |
|---|---|---|
| `emv` | VietQR EMV | app ngân hàng |
| `url` | URL trang thanh toán ví | app ví / camera |
| `image_url` | URL ảnh QR (dự phòng) | tuỳ |

**Thiết bị render `qrPayload` NGUYÊN VĂN.** Không parse, không dựng lại.

**Một intent chỉ giao hàng một lần.** Cloud gửi lại `payment_paid` tới khi có
`paid_ack`. Thiết bị nhớ từng `intentId` đã giao (bền qua reboot, chịu được hai
khách trả lệch thứ tự): frame gửi lại chỉ được ack, không giao lần hai. Chưa ghi
nhớ được (lỗi flash) thì thiết bị KHÔNG ack để cloud gửi lại sau.
Intent từng bị ngắt giữa lúc giao (mất điện) hoặc tới muộn sau khi đã rơi khỏi
cửa sổ nhớ: thiết bị ack nhưng KHÔNG giao, và gửi alert `paid_uncertain`
(critical, kèm intentId) — khách có thể đã trả mà chưa nhận, cần đối soát.

**Khi cổng lỗi:** cloud cố tình **không** phát QR local vô chủ — khách chuyển
tiền mà không kênh nào xác nhận là mất tiền thật. Thiết bị hiện `message` +
nút thử lại. Cloud đã tự báo đối tác qua alert `payment_gateway_down`.

## 7. Cấu hình vận hành

```
GET /api/device/config
    Device-Id: <MAC>
    Authorization: Bearer <device_token>
→ {"status":"ok","data":{"version":3,"name":"May rua xe cong A",
                         "config":{ ... }}}
```

Thiết bị lưu **chuỗi JSON thô** của `config` + `version` vào NVS. Trần **4096
byte**. Đọc từ cache khi khởi động — máy phải phục vụ được từ giây đầu, không
chờ mạng.

Chủ máy đổi cấu hình → cloud gửi:
```jsonc
{"type":"command","commandId":91,"action":"config_updated","params":{"version":4}}
```

**Lỗi mạng không được xoá cache cũ.** Máy offline vẫn phải bán đúng giá.

## 8. OTA

```
POST /ota/v1/
     Device-Id: <MAC>
     {"version":"1.2.17","application":{"version":"1.2.17"},
      "project":"<project_name trong image>","mac_address":"<MAC>"}
→ {"status":"ok","data":{"firmware":{
     "version":"1.2.18",          // X.Y.Z nghiêm ngặt
     "url":"https://.../ie-coin-1.2.18.bin",
     "sha256":"<64 hex>",         // BẮT BUỘC
     "size":1712384,              // BẮT BUỘC, byte
     "allowDowngrade":false}}}    // tuỳ chọn (true hoặc 1); mặc định chỉ nâng
   — không có bản mới thì bỏ "firmware".
```

Thiết bị: so version (chỉ nâng, trừ khi `allowDowngrade`) → tải HTTPS → kiểm
đúng `size` byte + SHA-256 → kiểm mô tả app trong image (cùng project, version
khớp manifest) → ghi partition dự phòng → reboot → **vào được cloud rồi mới xác
nhận** (huỷ rollback). Thiếu `sha256`/`size` hoặc URL không phải https = từ chối.

Thiết bị hỏi lúc boot, sau đó 6 giờ một lần; đang phục vụ khách thì 10 phút sau
hỏi lại. Lệnh nền tảng `ota_check` đánh thức việc hỏi ngay.

Bản mới treo/crash trước khi vào cloud → bootloader tự quay bản cũ ở lần reboot
kế. Đây là lý do **không được** gọi `esp_ota_mark_app_valid_cancel_rollback()`
sớm.

Cloud chỉ chào firmware cùng `project` (project_name ESP-IDF, khai bằng
`project()` trong CMakeLists) — một cloud phục vụ nhiều sản phẩm, máy không bao
giờ được chào image của sản phẩm khác. Thiếu `project` (firmware trước 0.2.1) =
sản phẩm mặc định của cloud. Cloud đọc product + version từ chính image lúc
upload; tên file do cloud đặt (`<project>-<version>.bin`). Rollout theo % hash MAC;
pin per-device để rollback từng máy.

## 9. Audio hai chiều — v1.1

Cùng kết nối WebSocket; text frame điều khiển, **binary frame (opcode 0x02)**
chở PCM. Định dạng khai trong `listen start`; v1.1 chỉ có `pcm16` mono 16 kHz.

```jsonc
// device → cloud
{"type":"listen","state":"start","format":"pcm16","rate":16000}
<binary: PCM16 LE mono, 20 ms = 640 byte mỗi frame> …
{"type":"listen","state":"stop"}

// cloud → device
{"type":"stt","text":"bật đèn"}            // cloud nghe được gì (hiện lên màn)
{"type":"tts","state":"start"}
<binary: PCM16 LE mono 16 kHz, 640 byte mỗi frame, gửi ĐÚNG NHỊP 20 ms>
{"type":"tts","state":"stop"}
```

- **Audio không vào hàng đợi bền.** Mất mạng là mất khung — đúng cho dữ liệu
  dòng; không ai muốn nghe lại câu nói trễ 30 giây.
- **Cloud phải gửi TTS đúng nhịp thật.** Đệm phát trên thiết bị nhỏ (~1,5 s);
  bắn cả clip một lúc là tràn và rơi tiếng.
- Thiết bị có thể gửi `listen start` khi cloud đang phát → cloud nên dừng TTS
  (barge-in). Thiết bị tự xả đệm phát khi bắt đầu thu.
- Trần một lần thu do hai bên tự đặt (thiết bị 15 s, cloud 20 s) — nút kẹt
  không được thành stream vô tận.
- Codec nén (Opus) là `format` khác trong tương lai; v1.1 không định nghĩa.

---

## Quy tắc thay đổi giao thức

1. **Không đổi ý nghĩa field đã có.** Thêm field mới, đừng đổi field cũ.
2. **Không bịa message type mới cho một dự án riêng.** Dùng lệnh động
   (`action` + `params`) — đó là chỗ để mở rộng.
3. **Không tin `tenant_id` do thiết bị gửi.** Quyền sở hữu do cloud quyết định
   từ `Device-Id` + token, không bao giờ từ payload.
4. **Mọi lệnh có side-effect phải có `commandId`** để chống trùng hoạt động.
5. **Thay đổi phá vỡ tương thích = protocol v2**, và v1 phải sống song song cho
   tới khi fleet flash xong. Máy ngoài hiện trường không cập nhật cùng lúc.
