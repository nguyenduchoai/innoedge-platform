# InnoEdge SDK cho Linux

Package Python `innoedge` dành cho máy tính nhúng Linux: Raspberry Pi, Banana Pi,
Orange Pi, Rockchip RK35xx, Jetson, x86. Dùng **cùng giao thức**
([PROTOCOL-v1](../../docs/PROTOCOL-v1.md)) và **cùng cam kết** với SDK ESP32:

- tiền và sự kiện được ghi xuống đĩa **trước** khi gửi, và gửi lại tới khi cloud
  ack (hàng đợi bền, ghi nguyên tử bằng fsync + rename);
- lệnh có side-effect **chạy tối đa một lần**: có nhật ký từng `commandId`; lệnh
  bị ngắt khi tắt máy được báo đối soát, không tự chạy lại;
- `on_paid` **không bao giờ tới code hai lần** cho cùng một `intentId`, kể cả khi
  cloud gửi lại hoặc hai khách trả lệch thứ tự;
- `innoedge.artifact`: cài bản phát hành (app, bundle mô hình, nội dung) có kiểm
  SHA-256 + kích thước; `current` đổi nguyên tử, rollback một lệnh.

## Cài đặt

```bash
pip install ./linux/python          # kéo theo websocket-client
sudo apt install -y python3-libgpiod   # tuỳ chọn, nếu điều khiển GPIO
```

Cần Python ≥ 3.8.

## Mã mẫu

```python
from innoedge import InnoEdge, PinRelay

app = InnoEdge(cloud_url="wss://cloud.example.com/ws/", token="<factory token>")
relay = PinRelay(pin=17, active_high=False, max_pulse_sec=10)

@app.on_qr
def show_qr(payload, amount, ref_code, expires_sec, intent_id):
    print("Render NGUYÊN VĂN lên màn hình:", payload)

@app.on_paid                      # tín hiệu DUY NHẤT được giao hàng
def paid(intent_id, amount_vnd):
    relay.pulse(seconds=5)

@app.command("dispense")          # chống trùng do SDK lo
def dispense(params):
    relay.pulse(seconds=min(params.get("seconds", 3), 10))
    return {"pulses": 1}

app.publish_payment("coin", 3)    # ghi đĩa trước, gửi sau
app.request_qr(25_000)
app.run()
```

Thử không cần tài khoản: chạy `go run ./tools/mock-cloud` rồi đặt
`cloud_url="ws://<ip>:8080/ws/"`.

## API

| | |
|---|---|
| `InnoEdge(cloud_url, device_id=None, token=None, fw_version, storage_dir=~/.innoedge, heartbeat_sec=30, heartbeat_extra=None)` | `device_id` mặc định là MAC. `heartbeat_extra()` trả dict, gửi kèm heartbeat dưới khoá `telemetry` |
| `start()` / `run()` / `stop()` | `run()` chặn tới Ctrl+C |
| `publish_payment(kind, count=0, amount_vnd=0)` | `kind`: `coin` · `ticket` · `coin_out` (đếm) · `cash` (VND) |
| `publish_event(name, data)` | `name` gồm `[A-Za-z0-9_.-]`, `data` là dict ≤ 4 KB |
| `request_qr(amount_vnd)` | `False` nếu đang offline |
| `alert(code, severity, message, active=True)` | có latch theo code; offline thì gửi sau |
| `@command(action)` | `handler(params) -> dict \| None`; `raise` = ack lỗi |
| `@on_paid` `@on_qr` `@on_qr_error` `@on_static_qr` `@on_assigned` `@on_unassigned` | |
| `@on_platform_command` | `(name, raw)` — `reboot`, `ota_check`… SDK Linux không tự reboot máy chủ |
| `is_online()` `is_assigned()` `queue_depth()` | |
| `artifact.install(url, sha256, size, root, version, validate=None)` · `artifact.rollback(root)` · `artifact.current_version(root)` | |

Handler chạy lần lượt trên một luồng riêng, nên handler chạy lâu (như tải bundle)
không làm nghẽn việc nhận khung từ cloud.

## Example

| | |
|---|---|
| [`examples/jumper/`](examples/jumper/) | Robot cua Jumper (RK3576): telemetry, cảnh báo, cài bundle `.app` có `--dry-run` + rollback |
| `examples/pi_vending_kiosk.py` | Kiosk bán nước 2 relay: QR → `on_paid` → rót |
| `examples/pi_digital_signage.py` | Bảng quảng cáo: proof-of-play, mua slot bằng QR |
| `examples/pi_central_audio.py` | Âm thanh đa vùng: lệnh phát thông báo theo zone |

## Test

```bash
python3 tests/test_linux_sdk.py          # logic: hàng đợi, nhật ký, QR, artifact
python3 tests/test_jumper_example.py     # example Jumper với bundle giả
python3 tests/test_linux_cloud.py        # phiên WebSocket thật với mock-cloud (cần Go)
```
