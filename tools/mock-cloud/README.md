# mock-cloud — InnoEdge Cloud giả

Chạy hết 8 example **không cần tài khoản, không cần internet, không cần database**.

```bash
go run ./tools/mock-cloud
```

```
InnoEdge mock-cloud — ĐỒ THỬ, đừng dùng cho production
  đặt cloud base URL của thiết bị = http://192.168.1.42:8080
  đơn giá: 1000 đ/xu · webhook giả sau 8s
```

Lấy địa chỉ nó in ra, đưa vào firmware:

```bash
idf.py menuconfig    # InnoEdge SDK → cloud base URL → http://192.168.1.42:8080
idf.py flash monitor
```

> Phải là **IP LAN**, không phải `127.0.0.1` — thiết bị nối từ WiFi, không phải
> từ máy bạn. Máy tính và ESP32 phải cùng mạng.

## Đây không phải sản phẩm

`mock-cloud` là **đồ thử**, không phải bản rút gọn của InnoEdge Cloud. Nó không
xác thực, không lưu gì, không đa tenant, không thanh toán thật, không dashboard.

Nó tồn tại vì hai lý do:

1. Bạn thấy thiết bị chạy trong 10 phút mà không phải đăng ký gì.
2. Chứng minh [`docs/PROTOCOL-v1.md`](../../docs/PROTOCOL-v1.md) là thật và ai
   cũng cài đặt lại được — bạn không bị khoá vào một nhà cung cấp.

**Đừng chạy ở production.** Nó nhận mọi token, tin mọi thiết bị.

## Chế độ AI: `-ai` — LLM cầm lái thay bàn phím

```bash
export ANTHROPIC_API_KEY=sk-ant-...      # hoặc: ant auth login
go run ./tools/mock-cloud -ai
> bật đèn rồi cho biết nhiệt độ
```

Claude tự chọn tool → mock biến thành lệnh InnoEdge → chờ ack → trả về cho
Claude làm tool result → Claude trả lời bằng **kết quả thật từ máy**. Cả vòng
lặp nằm trong `ai.go`, ~150 dòng, viết tay để đọc được từng bước. Firmware mẫu:
[`examples/09-ai-agent`](../../examples/09-ai-agent).

| Cờ | Mặc định | |
|---|---|---|
| `-model` | `claude-opus-5` | Model Claude |
| `-effort` | `medium` | `low` nhanh hơn cho demo; `high` khi lệnh nhiều bước |
| `-ack-wait` | `15s` | Máy không ack trong thời gian này → tool báo lỗi |

Test chế độ AI không cần API key: `go test ./...` giả lập cả LLM lẫn thiết bị.

## Điều khiển máy: gõ lệnh rồi Enter

```
ping                     kiểm tra sống
dispense <VND>           nhả tiền/credit      (example 06)
led <on|off>             bật/tắt LED          (example 03)
echo <chữ>               vọng lại             (example 03)
start_wash <combo>       mở phiên rửa xe      (example 08)
stop_wash                dừng phiên rửa       (example 08)
config_updated           báo cấu hình đã đổi  (example 04)
show_notice <chữ>        thông báo lên màn máy
reboot                   khởi động lại máy
raw {"a":1} tên_action   lệnh tự do

dup                      gửi LẠI lệnh vừa rồi với CÙNG commandId
ls                       liệt kê máy đang nối
h                        trợ giúp
```

## Bài thử đáng giá nhất: `dup`

Đây là thứ khiến máy **nhả tiền hai lần** nếu SDK làm sai.

```
> dispense 20000
  ← {"type":"command","commandId":1,"action":"dispense","params":{"amountVnd":20000}}
  → {"type":"command_ack","commandId":1,"status":"ok","message":"da nha 2 xung"}
  ✓ lệnh 1 → ok (da nha 2 xung)

> dup
gửi lại cùng commandId — máy phải trả "duplicate"
  → {"type":"command_ack","commandId":1,"status":"ok","message":"duplicate"}
```

Relay **không** kêu lần hai. Rút điện máy giữa chừng rồi cắm lại và gõ `dup` —
vẫn `duplicate`, vì watermark `commandId` nằm trong NVS.

## Bài thử số hai: rớt mạng không mất tiền

Với example 02 đang chạy:

1. Bấm nút BOOT vài lần → mock in `💰 coin: 1 xu = 1000 đ`.
2. **Tắt mock** (Ctrl-C).
3. Bấm BOOT 5 lần nữa → máy log `tồn 1,2,3,4,5`, mock không thấy gì.
4. **Chạy lại mock.**
5. Trong vài giây, 5 giao dịch chạy về hết, `tồn` về 0.

Rút điện máy ở bước 3 rồi cắm lại — hàng đợi vẫn còn nguyên.

## Tuỳ chọn

| Cờ | Mặc định | Ý nghĩa |
|---|---|---|
| `-addr` | `:8080` | Địa chỉ lắng nghe |
| `-rate` | `1000` | VND mỗi xu — dùng để tính `amountVND` trong ack |
| `-paid-after` | `8s` | Sau khi phát QR bao lâu thì giả lập webhook báo đã trả (`0` = không bao giờ) |
| `-fw` | — | Phiên bản firmware để chào OTA |
| `-fw-url` | — | URL **https** tải firmware |

```bash
go run ./tools/mock-cloud -rate 2000 -paid-after 3s
```

## OTA chỉ chạy được qua HTTPS

Thiết bị **từ chối** tải firmware qua `http://` — cố ý, không phải thiếu sót:
firmware không ký và không mã hoá đường truyền là cửa ngõ chiếm máy.

Nên mock mặc định luôn trả "không có bản mới". Example 05 vẫn chạy đủ (bạn thấy
luồng kiểm tra và `busy_check` hoãn cập nhật). Muốn thử tải thật thì đưa
`-fw-url` là một URL https có thật.

## Endpoint

| | |
|---|---|
| `GET  /ws/` | WebSocket thiết bị (header `Device-Id` + `Authorization: Bearer`) |
| `GET  /api/device/config` | Cấu hình vận hành — sửa `handleConfig()` để thử combo của bạn |
| `POST /ota/v1/` | Kiểm tra bản mới |

## Test

```bash
go test ./tools/mock-cloud
```

Dựng server thật, cho một thiết bị giả nối vào, kiểm tra từng khung tin đúng
spec: handshake, kích hoạt, ack tiền đúng đơn giá, QR + webhook, combo trong
config, và OTA mặc định không chào bản mới.
