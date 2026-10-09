// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// mock-cloud — InnoEdge Cloud giả, đủ để chạy hết 11 example mà KHÔNG cần tài
// khoản, không cần internet, không cần database.
//
//	go run ./tools/mock-cloud
//
// Rồi trỏ thiết bị vào máy bạn:
//
//	idf.py menuconfig → InnoEdge SDK → cloud base URL → http://<IP-LAN>:8080
//
// ĐÂY LÀ ĐỒ THỬ, KHÔNG PHẢI SẢN PHẨM. Nó không xác thực, không lưu gì, không
// đa tenant, không thanh toán thật. Mục đích duy nhất: để bạn thấy thiết bị
// chạy trong 10 phút và để chứng minh giao thức trong docs/PROTOCOL-v1.md là
// thật, ai cũng cài đặt lại được.
//
// TUYỆT ĐỐI không chạy cái này ở production.
package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/anthropics/anthropic-sdk-go"
	"github.com/gorilla/websocket"
)

var (
	addr      = flag.String("addr", ":8080", "địa chỉ lắng nghe")
	rateVND   = flag.Int64("rate", 1000, "đơn giá quy đổi: VND mỗi xu")
	paidAfter = flag.Duration("paid-after", 8*time.Second,
		"sau khi phát QR bao lâu thì giả lập webhook báo ĐÃ TRẢ (0 = không bao giờ)")
	fwVersion = flag.String("fw", "", "phiên bản firmware mới để chào OTA (rỗng = không có bản mới)")
	fwURL     = flag.String("fw-url", "", "URL https tải firmware (OTA yêu cầu HTTPS)")
)

// ── Phiên thiết bị ──────────────────────────────────────────────────────────

type device struct {
	id   string
	conn *websocket.Conn
	mu   sync.Mutex // websocket không cho ghi đồng thời
}

func (d *device) sendBinary(b []byte) {
	d.mu.Lock()
	defer d.mu.Unlock()
	if err := d.conn.WriteMessage(websocket.BinaryMessage, b); err != nil {
		log.Printf("  ✗ gửi binary tới %s lỗi: %v", d.id, err)
	}
}

func (d *device) send(v any) {
	d.mu.Lock()
	defer d.mu.Unlock()
	if err := d.conn.WriteJSON(v); err != nil {
		log.Printf("  ✗ gửi tới %s lỗi: %v", d.id, err)
		return
	}
	b, _ := json.Marshal(v)
	log.Printf("  ← %s", b)
}

var (
	devicesMu sync.RWMutex
	devices   = map[string]*device{}
	commandID int64
	intentID  int64
)

func nextCommandID() int64 { devicesMu.Lock(); defer devicesMu.Unlock(); commandID++; return commandID }
func nextIntentID() int64  { devicesMu.Lock(); defer devicesMu.Unlock(); intentID++; return intentID }

func pickDevice(id string) *device {
	devicesMu.RLock()
	defer devicesMu.RUnlock()
	if id != "" {
		return devices[id]
	}
	for _, d := range devices { // không chỉ định thì lấy máy bất kỳ đang nối
		return d
	}
	return nil
}

// ── WebSocket ───────────────────────────────────────────────────────────────

var upgrader = websocket.Upgrader{
	CheckOrigin: func(*http.Request) bool { return true }, // mock: nhận tất
}

func handleWS(w http.ResponseWriter, r *http.Request) {
	deviceID := r.Header.Get("Device-Id")
	token := r.Header.Get("Authorization")
	if deviceID == "" {
		http.Error(w, "thiếu header Device-Id", http.StatusBadRequest)
		return
	}
	// Mock KHÔNG kiểm tra token — chỉ in ra để bạn thấy thiết bị gửi cái gì.
	// Cloud thật đối chiếu hash token theo từng máy.
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close()

	d := &device{id: deviceID, conn: conn}
	devicesMu.Lock()
	devices[deviceID] = d
	devicesMu.Unlock()
	trackConnect(deviceID)
	defer func() {
		devicesMu.Lock()
		delete(devices, deviceID)
		devicesMu.Unlock()
		trackDisconnect(deviceID)
		log.Printf("✗ %s ngắt kết nối", deviceID)
	}()

	log.Printf("✓ %s kết nối (auth=%q)", deviceID, redact(token))

	for {
		mt, data, err := conn.ReadMessage()
		if err != nil {
			return
		}
		if mt == websocket.BinaryMessage {
			handleBinary(d, data) // PCM — không log từng khung
			continue
		}
		log.Printf("  → %s", data)
		handleFrame(d, data)
	}
}

func redact(auth string) string {
	t := strings.TrimPrefix(auth, "Bearer ")
	if len(t) <= 6 {
		return t
	}
	return t[:6] + "…" // đừng in token đầy đủ ra log, kể cả trong mock
}

func handleFrame(d *device, data []byte) {
	var env struct {
		Type      string          `json:"type"`
		Seq       *int64          `json:"seq"`
		Coins     int             `json:"coins"`
		Tickets   int             `json:"tickets"`
		Amount    int64           `json:"amount"`
		Method    string          `json:"method"`
		Code      string          `json:"code"`
		Severity  string          `json:"severity"`
		Message   string          `json:"message"`
		Active    *bool           `json:"active"`
		CommandID int64           `json:"commandId"`
		Status    string          `json:"status"`
		Result    json.RawMessage `json:"result"`
		Name      string          `json:"name"`  // event
		State     string          `json:"state"` // listen
		Data      json.RawMessage `json:"data"`  // event
		IntentID  int64           `json:"intentId"`
		FWVersion string          `json:"fw_version"`
		RSSI      int             `json:"rssi"`
		QueueDep  int             `json:"queue_depth"`
	}
	if err := json.Unmarshal(data, &env); err != nil {
		log.Printf("  ! JSON hỏng: %v", err)
		return
	}

	switch env.Type {
	case "hello":
		d.send(map[string]any{
			"type": "hello", "transport": "websocket",
			"session_id": fmt.Sprintf("mock-%d", time.Now().Unix()),
		})
		// Cloud thật gửi cái này khi chủ máy kích hoạt máy trên app. Mock tự
		// gửi sau 1s để bạn không phải làm gì — máy thành "đã gán" và dùng
		// được QR động.
		go func() {
			time.Sleep(time.Second)
			d.send(map[string]any{
				"type": "command", "command": "activation_complete",
				"authToken": "mock-device-token-" + d.id,
			})
			d.send(map[string]any{
				"type": "set_static_qr", "refCode": "GTMOCK" + d.id[len(d.id)-4:],
				"payload": "INNOEDGE-MOCK-STATIC-QR",
			})
		}()

	case "heartbeat":
		log.Printf("  ♥ fw=%s rssi=%d tồn=%d", env.FWVersion, env.RSSI, env.QueueDep)
		trackHeartbeat(d.id, env.FWVersion, env.RSSI, env.QueueDep)

	case "coin", "payment", "ticket":
		// Cloud thật dedupe theo (device_id, seq) rồi mới ack. Mock ack thẳng.
		coins := env.Coins
		amount := env.Amount
		method := env.Method
		if method == "" {
			method = "coin"
		}
		if env.Type == "coin" || method == "coin" {
			amount = int64(coins) * *rateVND
		}
		if env.Type == "ticket" {
			log.Printf("  🎟  %d vé hủy (không tính doanh thu)", env.Tickets)
		} else {
			log.Printf("  💰 %s: %d xu = %d đ", method, coins, amount)
		}
		trackPayment(d.id, method, coins, amount)
		ack := map[string]any{
			"type": "coin_ack", "status": "ok", "method": method,
			"coins": coins, "rateVND": *rateVND, "amountVND": amount,
			"duplicate": false,
		}
		if env.Seq != nil {
			ack["seq"] = *env.Seq
		}
		d.send(ack)

	case "listen":
		handleListen(d, env.State)

	case "event":
		// Sự kiện tuỳ ý (v1.1): ack theo seq để gỡ khỏi hàng đợi bền của máy,
		// rồi đưa cho AI (nếu -ai) như một tin từ thế giới thật.
		log.Printf("  ⚡ sự kiện %s %s", env.Name, env.Data)
		// Vào hàng đợi AI TRƯỚC, ack SAU. Ack rồi mới drop = máy đã xoá khỏi
		// hàng đợi bền → mất hẳn câu trả lời của bé.
		if notifyDeviceEvent(d.id, env.Name, env.Data) && env.Seq != nil {
			d.send(map[string]any{"type": "event_ack", "seq": *env.Seq})
		}

	case "alert":
		state := "BẬT"
		active := env.Active != nil && *env.Active
		if env.Active != nil && !*env.Active {
			state = "hết"
		}
		log.Printf("  ⚠  cảnh báo %s [%s] %s — %s", state, env.Severity, env.Code, env.Message)
		trackAlert(d.id, env.Code, env.Severity, env.Message, active)

	case "qr_request":
		id := nextIntentID()
		ref := fmt.Sprintf("GTMOCKD%05d", id)
		trackQR(d.id, ref, id, env.Amount)
		d.send(map[string]any{
			"type": "qr", "seq": env.Seq, "intentId": id, "refCode": ref,
			"amount": env.Amount,
			// Chuỗi giả rõ ràng — KHÔNG phải VietQR thật, quét cũng không
			// chuyển được tiền cho ai.
			"qrPayload":     fmt.Sprintf("INNOEDGE-MOCK-QR:%s:%d", ref, env.Amount),
			"payloadFormat": "emv", "expiresSec": 300,
		})
		if *paidAfter > 0 {
			// Giả lập webhook ngân hàng báo tiền về.
			go func(amount int64) {
				time.Sleep(*paidAfter)
				log.Printf("  🏦 giả lập webhook: intent %d đã trả %d đ", id, amount)
				d.send(map[string]any{
					"type": "payment_paid", "intentId": id,
					"amount": amount, "refCode": ref,
				})
				trackPaymentPaid(d.id, ref, id, amount)
			}(env.Amount)
		}

	case "command_ack":
		log.Printf("  ✓ lệnh %d → %s (%s)", env.CommandID, env.Status, env.Message)
		trackCommandAck(d.id, env.CommandID, env.Status, env.Message)
		deliverAck(env.CommandID, ackResult{Status: env.Status, Message: env.Message, Result: env.Result})

	case "paid_ack":
		log.Printf("  ✓ máy đã xử lý payment_paid intent=%d", env.IntentID)

	default:
		log.Printf("  ? kiểu chưa hỗ trợ: %s", env.Type)
	}
}

// ── REST ────────────────────────────────────────────────────────────────────

// GET /api/device/config — cấu hình vận hành. Thiết bị cache vào NVS để chạy
// offline. Sửa hàm này để thử example 04 và 08 với combo của bạn.
func handleConfig(w http.ResponseWriter, r *http.Request) {
	log.Printf("↓ GET /api/device/config  device=%s", r.Header.Get("Device-Id"))
	writeJSON(w, map[string]any{
		"status": "ok",
		"data": map[string]any{
			"version": 3,
			"name":    "Máy demo (mock-cloud)",
			"config": map[string]any{
				"pricing": map[string]any{"coinPerBillVnd": *rateVND, "coinPerQrVnd": *rateVND},
				"dynamic": map[string]any{"ui_language": "vi"},
				"combos": []map[string]any{
					{"id": "A", "name": "Rửa cơ bản", "priceVnd": 40000,
						"payload": map[string]any{"steps": []map[string]any{
							{"device": "water", "seconds": 120},
							{"device": "foam", "seconds": 60},
						}}},
					{"id": "B", "name": "Rửa full", "priceVnd": 70000,
						"payload": map[string]any{"steps": []map[string]any{
							{"device": "water", "seconds": 180},
							{"device": "foam", "seconds": 90},
							{"device": "air", "seconds": 60},
							{"device": "vacuum", "seconds": 120},
						}}},
				},
			},
		},
	})
}

// POST /ota/v1/ — kiểm tra bản mới.
//
// Lưu ý: thiết bị TỪ CHỐI tải firmware qua http:// (xem ie_ota_client.c).
// Nên mock chỉ chào được bản mới nếu bạn đưa -fw-url là một URL https thật.
// Không có cờ đó thì luôn trả "không có bản mới" — vẫn đủ để example 05 chạy.
func handleOTA(w http.ResponseWriter, r *http.Request) {
	log.Printf("↓ POST /ota/v1/  device=%s", r.Header.Get("Device-Id"))
	data := map[string]any{
		"assignment": map[string]any{"status": "assigned"},
	}
	if *fwVersion != "" && *fwURL != "" {
		data["firmware"] = map[string]any{"version": *fwVersion, "url": *fwURL}
		log.Printf("  ← chào bản mới %s", *fwVersion)
	}
	writeJSON(w, map[string]any{"status": "ok", "data": data})
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

// ── Bàn phím: gõ lệnh, máy làm ngay ─────────────────────────────────────────

const helpText = `
Gõ lệnh rồi Enter (máy đang nối sẽ nhận ngay):

  ping                     kiểm tra sống
  dispense <VND>           nhả tiền/credit      (example 06)
  led <on|off>             bật/tắt LED          (example 03)
  echo <chữ>               vọng lại             (example 03)
  start_wash <combo>       mở phiên rửa xe      (example 08)
  stop_wash                dừng phiên rửa       (example 08)
  config_updated           báo cấu hình đã đổi  (example 04)
  show_notice <chữ>        thông báo lên màn máy
  reboot                   khởi động lại máy
  raw <json params> <action>   lệnh tự do

  dup                      gửi LẠI lệnh vừa rồi với CÙNG commandId
                           → phải thấy máy trả "duplicate", KHÔNG chạy hai lần
  ls                       liệt kê máy đang nối
  h                        trợ giúp này
`

func consoleLoop() {
	fmt.Print(helpText)
	sc := bufio.NewScanner(os.Stdin)
	var last map[string]any

	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, " ", 2)
		cmd, arg := parts[0], ""
		if len(parts) > 1 {
			arg = strings.TrimSpace(parts[1])
		}

		switch cmd {
		case "h", "help":
			fmt.Print(helpText)
			continue
		case "ls":
			devicesMu.RLock()
			if len(devices) == 0 {
				fmt.Println("(chưa máy nào nối)")
			}
			for id := range devices {
				fmt.Println(" •", id)
			}
			devicesMu.RUnlock()
			continue
		case "dup":
			if last == nil {
				fmt.Println("(chưa gửi lệnh nào)")
				continue
			}
			if d := pickDevice(""); d != nil {
				fmt.Println("gửi lại cùng commandId — máy phải trả \"duplicate\"")
				d.send(last)
			}
			continue
		}

		params := map[string]any{}
		action := cmd
		switch cmd {
		case "dispense":
			n, err := strconv.ParseInt(arg, 10, 64)
			if err != nil || n <= 0 {
				fmt.Println("dùng: dispense 20000")
				continue
			}
			params["amountVnd"] = n
		case "led":
			params["on"] = arg == "on" || arg == "true" || arg == "1"
		case "echo":
			params["text"] = arg
		case "start_wash":
			if arg == "" {
				arg = "A"
			}
			params["combo"] = arg
		case "show_notice":
			params["title"] = "Thông báo"
			params["message"] = arg
			params["durationSec"] = 10
		case "config_updated":
			params["version"] = 4
		case "raw":
			f := strings.SplitN(arg, " ", 2)
			if len(f) != 2 {
				fmt.Println(`dùng: raw {"a":1} tên_action`)
				continue
			}
			if err := json.Unmarshal([]byte(f[0]), &params); err != nil {
				fmt.Println("params không phải JSON hợp lệ:", err)
				continue
			}
			action = f[1]
		case "ping", "stop_wash", "reboot":
			// không có params
		default:
			fmt.Printf("không rõ lệnh %q — gõ h để xem trợ giúp\n", cmd)
			continue
		}

		d := pickDevice("")
		if d == nil {
			fmt.Println("(chưa máy nào nối — flash example rồi thử lại)")
			continue
		}
		frame := map[string]any{
			"type": "command", "commandId": nextCommandID(),
			"action": action, "params": params,
		}
		last = frame
		lastSentFrame = frame
		d.send(frame)
	}
}

// newMux khai báo toàn bộ endpoint. Tách riêng để test dựng được server thật
// trên cổng ngẫu nhiên.
func newMux() *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("/ws/", handleWS)
	mux.HandleFunc("/api/device/config", handleConfig)
	mux.HandleFunc("/ota/v1/", handleOTA)
	mux.HandleFunc("/ota/v1", handleOTA)
	registerDashboard(mux)
	return mux
}

func main() {
	flag.Parse()
	log.SetFlags(log.Ltime)

	fmt.Println("InnoEdge mock-cloud — ĐỒ THỬ, đừng dùng cho production")
	for _, ip := range localIPs() {
		fmt.Printf("  đặt cloud base URL của thiết bị = http://%s%s\n", ip, *addr)
	}
	fmt.Printf("  mở web dashboard console     = http://localhost%s hoặc http://%s%s\n", *addr, localIPs()[0], *addr)
	fmt.Printf("  mở web ble provisioning      = http://localhost%s/provision/\n", *addr)
	fmt.Printf("  đơn giá: %d đ/xu · webhook giả sau %s\n", *rateVND, *paidAfter)

	if *voiceMode && !*aiMode {
		log.Fatal("-voice cần -ai (ai trả lời câu nói?)")
	}
	if *voiceMode {
		if err := voiceReady(); err != nil {
			log.Fatal(err)
		}
		ab, _, am := asrConfig()
		tb, _, tm, tv := ttsConfig()
		fmt.Printf("  giọng nói: ASR %s (%s @ %s) · TTS %s (%s, giọng %q @ %s)\n",
			*asrKind, am, ab, *ttsKind, tm, tv, tb)
	}
	if *aiMode {
		go aiLoop(anthropic.NewClient())
	} else {
		go consoleLoop()
	}
	log.Fatal(http.ListenAndServe(*addr, newMux()))
}

// localIPs trả về IP LAN để bạn khỏi phải đi tra — thiết bị cần địa chỉ này,
// không phải 127.0.0.1.
func localIPs() []string {
	var out []string
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return []string{"<IP-LAN-cua-ban>"}
	}
	for _, a := range addrs {
		if n, ok := a.(*net.IPNet); ok && !n.IP.IsLoopback() && n.IP.To4() != nil {
			out = append(out, n.IP.String())
		}
	}
	if len(out) == 0 {
		out = []string{"<IP-LAN-cua-ban>"}
	}
	return out
}
