# innoedge-mcp — MCP server cho công cụ AI coding

Nạp đúng API, đúng giao thức, đúng luật vào ngữ cảnh của AI **trước khi** nó gõ
dòng code đầu tiên.

## Vấn đề nó giải

AI viết firmware IoT rất hay bịa:

- bịa tên hàm không tồn tại
- bịa MQTT topic — InnoEdge dùng **WebSocket**, không có topic nào
- tự parse rồi dựng lại chuỗi QR (làm mã hỏng)
- tự viết lại chống trùng lệnh (làm máy nhả tiền hai lần)

Server này đưa cho AI sự thật thay vì để nó đoán.

## Cài

```bash
cd tools/mcp && go build -o innoedge-mcp .
```

Một binary, thư viện chuẩn Go, **không npm, không node_modules**.

### Claude Code

```bash
claude mcp add innoedge -- /đường/dẫn/tuyệt/đối/innoedge-mcp -root /đường/dẫn/tới/innoedge-platform
```

### Cursor / VS Code — `.cursor/mcp.json`

```json
{
  "mcpServers": {
    "innoedge": {
      "command": "/đường/dẫn/tuyệt/đối/innoedge-mcp",
      "args": ["-root", "/đường/dẫn/tới/innoedge-platform"]
    }
  }
}
```

`-root` có thể bỏ nếu chạy từ trong repo — server tự dò ngược lên tìm
`components/innoedge/include/innoedge.h`. Đưa đường dẫn tuyệt đối thì chắc chắn hơn.

## Tool

| Tool | Trả về |
|---|---|
| `innoedge_api` | Toàn bộ `innoedge.h` — **chỉ những hàm ở đây mới tồn tại** |
| `innoedge_protocol` | `PROTOCOL-v1.md`, lọc được theo mục (`section: "OTA"`) |
| `innoedge_example` | Mã nguồn một example (bỏ trống `name` để liệt kê) |
| `innoedge_rules` | 22 luật bắt buộc — mỗi luật là một sự cố đã xảy ra thật |

## Dùng thế nào

Bảo AI như bình thường, nó tự gọi tool:

> *"Viết firmware cho máy bán nước: nhận xu, cloud gửi lệnh mở van 30 giây."*

AI sẽ gọi `innoedge_rules` + `innoedge_api`, thấy `innoedge_publish_payment()`
và `innoedge_register_command()`, rồi lấy `06-coin-relay` làm khuôn — thay vì
nghĩ ra một kiến trúc mới và một MQTT topic không tồn tại.

Khi cần chắc chắn:

> *"Đọc innoedge_rules trước rồi mới sửa."*

## Test

```bash
go test ./tools/mcp
```

Chạy binary thật, nói JSON-RPC qua stdio đúng như client sẽ làm: vọng lại
`protocolVersion`, không trả lời notification (trả lời là client treo), liệt kê
tool đủ `inputSchema`, cắt đúng mục của spec, khớp tên example gần đúng, và lỗi
tool trả qua `isError` kèm danh sách tên hợp lệ để AI tự sửa.
