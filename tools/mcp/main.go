// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// innoedge-mcp — MCP server cho công cụ AI coding (Claude Code, Cursor, VS Code).
//
// Vấn đề nó giải: AI viết firmware IoT rất hay BỊA. Bịa tên hàm không có, bịa
// MQTT topic (InnoEdge dùng WebSocket), tự dựng lại chuỗi QR, tự viết lại chống
// trùng lệnh. Server này nạp đúng API, đúng giao thức, đúng luật vào ngữ cảnh
// của AI trước khi nó gõ dòng đầu tiên.
//
// JSON-RPC 2.0 qua stdio, chỉ dùng thư viện chuẩn — không npm, không node_modules.
package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

var root = flag.String("root", "", "thư mục gốc repo SDK (rỗng = tự dò)")

// ── JSON-RPC ────────────────────────────────────────────────────────────────

type request struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id"` // vắng mặt = notification, KHÔNG trả lời
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params"`
}

type response struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id"`
	Result  any             `json:"result,omitempty"`
	Error   *rpcError       `json:"error,omitempty"`
}

type rpcError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

// ── Tool ────────────────────────────────────────────────────────────────────

type tool struct {
	Name        string         `json:"name"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
	run         func(map[string]any) (string, error)
}

func obj(props map[string]any, required ...string) map[string]any {
	if required == nil {
		required = []string{}
	}
	return map[string]any{"type": "object", "properties": props, "required": required}
}

func str(desc string) map[string]any { return map[string]any{"type": "string", "description": desc} }

func tools() []tool {
	return []tool{
		{
			Name: "innoedge_api",
			Description: "API công khai của InnoEdge SDK (nội dung innoedge.h). ĐỌC CÁI NÀY " +
				"TRƯỚC khi viết bất kỳ dòng firmware InnoEdge nào. Chỉ những hàm ở đây mới tồn tại.",
			InputSchema: obj(map[string]any{}),
			run:         func(map[string]any) (string, error) { return read("components/innoedge/include/innoedge.h") },
		},
		{
			Name: "innoedge_protocol",
			Description: "Đặc tả giao thức thiết bị ↔ cloud (PROTOCOL-v1.md): handshake, heartbeat, " +
				"khung tiền, cảnh báo, lệnh động, QR, cấu hình, OTA. Dùng khi cần biết hình dạng " +
				"chính xác của một khung tin, hoặc khi viết phía server gửi lệnh xuống máy.",
			InputSchema: obj(map[string]any{
				"section": str("lọc theo tiêu đề mục, vd \"lệnh động\", \"QR\", \"OTA\". Bỏ trống = toàn bộ."),
			}),
			run: func(a map[string]any) (string, error) {
				s, err := read("docs/PROTOCOL-v1.md")
				if err != nil {
					return "", err
				}
				return section(s, argStr(a, "section")), nil
			},
		},
		{
			Name: "innoedge_example",
			Description: "Mã nguồn một example đã chạy được. Bỏ trống name để liệt kê. " +
				"Dùng làm khuôn mẫu thay vì tự nghĩ ra cấu trúc mới.",
			InputSchema: obj(map[string]any{
				"name": str("tên example, vd \"06-coin-relay\" hoặc chỉ \"coin\". Bỏ trống = liệt kê."),
			}),
			run: func(a map[string]any) (string, error) { return example(argStr(a, "name")) },
		},
		{
			Name: "innoedge_rules",
			Description: "Luật bắt buộc khi viết firmware InnoEdge. ĐỌC TRƯỚC KHI SỬA CODE. " +
				"Đây là các lỗi đã thực sự làm mất tiền ngoài hiện trường.",
			InputSchema: obj(map[string]any{}),
			run:         func(map[string]any) (string, error) { return rulesText, nil },
		},
	}
}

const rulesText = `# Luật viết firmware InnoEdge

Mỗi luật dưới đây tương ứng một sự cố đã xảy ra thật trên máy đang vận hành.

## Kiến trúc
1. Dùng API công khai ` + "`innoedge_*`" + `. KHÔNG gọi thẳng WebSocket/MQTT/HTTP.
   Chạy tool innoedge_api để xem danh sách hàm có thật — đừng bịa.
2. Transport là **WebSocket**, KHÔNG phải MQTT. Không có topic nào để tạo.
3. Nghiệp vụ sản phẩm (motor, giá bán, màn hình, luồng khách) KHÔNG được đặt
   vào components/innoedge/. Phải nằm ở lớp application.
4. KHÔNG hard-code GPIO trong SDK. Chân cắm thuộc lớp board (Kconfig của project).

## Tiền — sai là mất tiền thật
5. Ghi nhận tiền LUÔN qua innoedge_publish_payment(). Nó ghi NVS trước rồi mới
   gửi. Đừng tự viết hàng đợi.
6. Gửi SỐ XU, để cloud quy đổi ra tiền. Mỗi đối tác một đơn giá, đổi được từ app.
   Nhân giá trong firmware = phải flash lại cả fleet khi đổi giá.
7. KHÔNG tự viết chống trùng lệnh. SDK giữ nhật ký từng commandId trong NVS:
   ghi "đang chạy" TRƯỚC handler, "xong/lỗi" TRƯỚC ack. Lệnh bị mất điện cắt ngang
   được báo đối soát, không tự chạy lại. Viết lại = nhả tiền hai lần hoặc bỏ lệnh.
8. Lệnh nhả tiền phải có TRẦN an toàn (số xung tối đa). Một params sai không được
   biến thành lệnh xả sạch hopper.

## Lệnh từ xa
9. Handler chạy trên task WebSocket — KHÔNG block quá vài giây. Việc nặng thì
   xTaskCreate rồi trả ESP_OK ngay.
10. KHÔNG gọi esp_restart() trong handler. Dùng innoedge_reboot_after_ack(),
    nếu không máy reboot trước khi ack kịp gửi và cloud gửi lại lệnh.
11. Chuỗi action truyền cho innoedge_register_command() phải là string literal —
    registry giữ con trỏ, không copy.

## QR / thanh toán
12. Render qrPayload NGUYÊN VĂN. KHÔNG parse, KHÔNG dựng lại. Nội dung khác nhau
    theo cổng (EMV VietQR / URL ví / URL ảnh).
13. CHỈ callback on_paid mới được phép giao hàng. Không phải "khách bảo đã chuyển",
    không phải "QR đã hiện đủ lâu".
14. on_qr_error phải hiện được cho khách. Cloud cố tình không phát QR vô chủ khi
    cổng lỗi — khách chuyển tiền mà không kênh nào xác nhận là mất tiền thật.

## Cấu hình / OTA
15. Đọc cấu hình từ cache NVS TRƯỚC khi lên mạng. Máy phải phục vụ được từ giây đầu.
16. Lỗi mạng KHÔNG được xoá cache cũ. Offline vẫn phải bán đúng giá.
17. KHÔNG gọi esp_ota_mark_app_valid_cancel_rollback(). SDK gọi khi WebSocket kết
    nối lần đầu. Gọi sớm = vô hiệu hoá rollback = máy chết ngoài hiện trường.
18. Khai busy_check nếu máy có lúc đang phục vụ khách, để OTA không reboot giữa
    lúc khách trả tiền.
18b. Phiên bản firmware = PROJECT_VER trong CMakeLists (X.Y.Z). KHÔNG đặt thêm
    cfg.fw_version: hai nguồn lệch nhau là OTA tải lại mãi một bản.

## Bảo mật
19. KHÔNG log device_token, access token, hay khoá riêng.
20. KHÔNG hard-code secret production trong source. Nạp ở khâu factory provisioning.

## Xong việc
21. Sửa giao thức thì phải sửa CẢ docs/PROTOCOL-v1.md lẫn phía server.
22. Logic không tầm thường thì để lại một kiểm tra chạy được (xem tests/).
`

// ── Đọc file ────────────────────────────────────────────────────────────────

func read(rel string) (string, error) {
	b, err := os.ReadFile(filepath.Join(*root, rel))
	if err != nil {
		return "", fmt.Errorf("không đọc được %s: %w (kiểm tra cờ -root)", rel, err)
	}
	return string(b), nil
}

// section cắt lấy mục có tiêu đề khớp query (không phân biệt hoa thường).
func section(md, query string) string {
	if query == "" {
		return md
	}
	q := strings.ToLower(query)
	lines := strings.Split(md, "\n")
	var out []string
	depth, capturing := 0, false
	for _, ln := range lines {
		if strings.HasPrefix(ln, "#") {
			d := len(ln) - len(strings.TrimLeft(ln, "#"))
			if capturing && d <= depth {
				capturing = false
			}
			if !capturing && strings.Contains(strings.ToLower(ln), q) {
				capturing, depth = true, d
			}
		}
		if capturing {
			out = append(out, ln)
		}
	}
	if len(out) == 0 {
		return fmt.Sprintf("Không thấy mục nào khớp %q. Gọi lại không kèm section để xem toàn bộ.", query)
	}
	return strings.Join(out, "\n")
}

func example(name string) (string, error) {
	dir := filepath.Join(*root, "examples")
	entries, err := os.ReadDir(dir)
	if err != nil {
		return "", fmt.Errorf("không đọc được examples/: %w (kiểm tra cờ -root)", err)
	}
	var names []string
	for _, e := range entries {
		if e.IsDir() {
			names = append(names, e.Name())
		}
	}
	sort.Strings(names)

	if name == "" {
		return "Các example có sẵn (gọi lại kèm name):\n  " + strings.Join(names, "\n  "), nil
	}
	var match string
	for _, n := range names {
		if strings.Contains(strings.ToLower(n), strings.ToLower(name)) {
			match = n
			break
		}
	}
	if match == "" {
		return "", fmt.Errorf("không có example %q. Có: %s", name, strings.Join(names, ", "))
	}

	var b strings.Builder
	for _, f := range []string{"README.md", "main/app_main.c", "main/CMakeLists.txt", "sdkconfig.defaults"} {
		data, err := os.ReadFile(filepath.Join(dir, match, f))
		if err != nil {
			continue // sdkconfig.defaults chỉ có ở example dùng phần cứng (chân cắm)
		}
		fmt.Fprintf(&b, "\n===== %s/%s =====\n%s", match, f, data)
	}
	return b.String(), nil
}

func argStr(a map[string]any, k string) string {
	s, _ := a[k].(string)
	return s
}

// ── Vòng lặp MCP ────────────────────────────────────────────────────────────

func main() {
	flag.Parse()
	if *root == "" {
		*root = findRoot()
	}

	all := tools()
	byName := map[string]tool{}
	for _, t := range all {
		byName[t.Name] = t
	}

	in := bufio.NewScanner(os.Stdin)
	in.Buffer(make([]byte, 0, 64*1024), 8*1024*1024) // README + spec có thể dài
	out := json.NewEncoder(os.Stdout)

	for in.Scan() {
		line := in.Bytes()
		if len(line) == 0 {
			continue
		}
		var req request
		if err := json.Unmarshal(line, &req); err != nil {
			continue
		}
		// Notification (không có id) → xử lý xong im lặng, KHÔNG trả lời.
		if len(req.ID) == 0 {
			continue
		}
		resp := response{JSONRPC: "2.0", ID: req.ID}

		switch req.Method {
		case "initialize":
			var p struct {
				ProtocolVersion string `json:"protocolVersion"`
			}
			_ = json.Unmarshal(req.Params, &p)
			if p.ProtocolVersion == "" {
				p.ProtocolVersion = "2024-11-05"
			}
			resp.Result = map[string]any{
				"protocolVersion": p.ProtocolVersion, // vọng lại để hợp mọi client
				"capabilities":    map[string]any{"tools": map[string]any{}},
				"serverInfo":      map[string]any{"name": "innoedge-sdk", "version": "0.1.0"},
				"instructions": "SDK InnoEdge cho ESP32. Trước khi viết hoặc sửa firmware InnoEdge: " +
					"gọi innoedge_rules và innoedge_api. Transport là WebSocket, KHÔNG phải MQTT. " +
					"Chỉ dùng hàm có trong innoedge_api — đừng bịa tên hàm.",
			}

		case "tools/list":
			list := make([]map[string]any, 0, len(all))
			for _, t := range all {
				list = append(list, map[string]any{
					"name": t.Name, "description": t.Description, "inputSchema": t.InputSchema,
				})
			}
			resp.Result = map[string]any{"tools": list}

		case "tools/call":
			var p struct {
				Name string         `json:"name"`
				Args map[string]any `json:"arguments"`
			}
			_ = json.Unmarshal(req.Params, &p)
			t, ok := byName[p.Name]
			if !ok {
				resp.Error = &rpcError{Code: -32602, Message: "không có tool " + p.Name}
				break
			}
			text, err := t.run(p.Args)
			if err != nil {
				// Lỗi của tool trả qua isError, KHÔNG phải lỗi JSON-RPC —
				// để AI đọc được và tự sửa (vd chỉnh lại -root).
				resp.Result = map[string]any{
					"content": []map[string]any{{"type": "text", "text": err.Error()}},
					"isError": true,
				}
				break
			}
			resp.Result = map[string]any{
				"content": []map[string]any{{"type": "text", "text": text}},
			}

		case "ping":
			resp.Result = map[string]any{}

		default:
			resp.Error = &rpcError{Code: -32601, Message: "chưa hỗ trợ method " + req.Method}
		}

		_ = out.Encode(resp)
	}
}

// findRoot dò ngược lên từ thư mục hiện tại rồi từ vị trí binary, tìm gốc repo
// SDK. Có cờ -root thì không cần hàm này.
func findRoot() string {
	marker := filepath.Join("components", "innoedge", "include", "innoedge.h")
	var starts []string
	if wd, err := os.Getwd(); err == nil {
		starts = append(starts, wd)
	}
	if exe, err := os.Executable(); err == nil {
		starts = append(starts, filepath.Dir(exe))
	}
	for _, dir := range starts {
		for i := 0; i < 6; i++ {
			if _, err := os.Stat(filepath.Join(dir, marker)); err == nil {
				return dir
			}
			parent := filepath.Dir(dir)
			if parent == dir {
				break
			}
			dir = parent
		}
	}
	return "." // để tool báo lỗi rõ ràng kèm gợi ý -root
}
