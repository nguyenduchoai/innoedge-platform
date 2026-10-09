# Jumper × InnoEdge — quản lý đội robot cua

[Jumper](https://github.com/KingKongRobotics/jumper) là robot cua 22 khớp chạy
Rockchip RK3576 (Ubuntu 22.04, kernel PREEMPT_RT). Example này nối nó vào
InnoEdge bằng SDK Linux để **vận hành cả đội robot từ xa**.

## InnoEdge làm gì, KHÔNG làm gì

| Làm | Không làm |
|---|---|
| Heartbeat kèm tình trạng: controller, bundle đang chạy, nhiệt SoC | Lái khớp, đổi dáng đi, ra lệnh nhảy/múa qua cloud |
| Cảnh báo khi controller dừng hoặc SoC ≥ 85 °C | Đụng vào vòng điều khiển 1 kHz |
| Cài bundle `.app` từ xa: SHA-256 + size + `controller --dry-run` (trong sandbox systemd: không mạng, không thiết bị) trước khi chuyển | Tự khởi động lại controller khi robot có thể đang đứng |
| Rollback một lệnh; restart controller khi người vận hành xác nhận | |

Lý do: các chốt an toàn của Jumper (nghiêng quá 50°, mất phản hồi động cơ 100 ms,
mất lệnh 500 ms) nằm trong controller. Một lệnh đi qua Internet trễ hàng trăm
mili-giây không có chỗ trong vòng đó. Controller hiện chỉ nhận lệnh từ tay cầm
qua DDS. Muốn "khách quét QR → robot múa" thì Jumper cần thêm một cổng xin-đổi-mode
cục bộ, đi qua đúng các chốt an toàn ấy. Phần đó phải làm ở phía Jumper, không phải
ở SDK.

## Chạy thử (không cần robot)

```bash
pip install websocket-client
go run ./tools/mock-cloud                                     # terminal 1
python3 linux/python/examples/jumper/jumper_fleet.py \
    --cloud ws://127.0.0.1:8080/ws/ --bundle-root /tmp/mjrl --storage /tmp/ie-jumper   # terminal 2
```

Trên dashboard mock-cloud, gửi lệnh `robot_status`.

## Cài lên robot

```bash
sudo pip install innoedge          # hoặc: pip install ./linux/python
sudo cp jumper_fleet.py /usr/local/bin/innoedge-jumper
sudo cp innoedge-jumper.service /etc/systemd/system/
sudo systemctl edit innoedge-jumper   # đặt INNOEDGE_WS + tên unit controller
sudo systemctl enable --now innoedge-jumper
```

Controller của Jumper chạy `--bundle /opt/mjrl/current`. Example quản lý đúng
thư mục đó:

```
/opt/mjrl/releases/<version>/   mỗi bản một thư mục
/opt/mjrl/current  -> releases/<version>    đổi nguyên tử
/opt/mjrl/previous -> releases/<cũ>
```

## Lệnh từ cloud

| action | params | kết quả |
|---|---|---|
| `robot_status` | — | `{controller, bundle, soc_temp_c}` |
| `bundle_install` | `url` (https), `sha256`, `size`, `version` | `{version, previous, restart_required}` |
| `bundle_rollback` | — | `{version}` |
| `controller_restart` | `{"confirm": true}` | `{controller}` |

`bundle_install` **không** tự khởi động lại controller. Bản mới có hiệu lực sau
`controller_restart`, và chỉ gửi lệnh đó khi robot đang nằm nghỉ.

Mọi lệnh đi qua nhật ký chống trùng của SDK. Cloud gửi lại cùng `commandId` thì
lệnh không chạy lần hai.

## Đã kiểm

- `tests/test_jumper_example.py`: dùng bundle giả. Cài, chặn bundle hỏng
  `--dry-run`, rollback, chặn lệnh trùng.
- Đã cài thử `jumper.app` thật (26 MB, 12 mode) bằng `innoedge.artifact`: binary
  `runtime/board/controller` giữ quyền chạy sau khi giải nén.
- **Chưa chạy trên robot thật.** `--dry-run` của binary aarch64 chỉ chạy được trên board.

---

## English

Fleet operations for the Jumper crab robot (RK3576) on top of the InnoEdge Linux
SDK. It provides heartbeat telemetry (controller state, active bundle, SoC
temperature), alerts, and remote `.app` bundle install. Each install is checked
for SHA-256 and size, and runs `controller --dry-run` on the robot before
`/opt/mjrl/current` is switched atomically. One-command rollback is supported,
and the controller restarts only when the operator confirms. **No joint control
over the cloud.** Jumper's safety interlocks (tilt, 100 ms feedback, 500 ms
command timeouts) live in its own 1 kHz controller. Pay-to-play ("scan QR → robot
dances") would need a local mode-request input on Jumper's side that goes through
those interlocks. Not yet tested on real hardware.
