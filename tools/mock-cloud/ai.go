// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Chế độ -ai: LLM cầm lái thay bàn phím.
//
// Người dùng gõ tiếng người ("bật đèn rồi cho biết nhiệt độ"), Claude tự quyết
// định gọi tool nào, mock-cloud biến tool call thành lệnh InnoEdge gửi xuống
// ESP32, chờ ack, trả ack về cho Claude làm tool result, Claude trả lời bằng
// kết quả THẬT từ thiết bị.
//
//	người → LLM → tool_use → {"type":"command",...} → ESP32
//	người ← LLM ← tool_result ← {"type":"command_ack",...} ← ESP32
//
// Đây là mảnh "AI quyết định hành động → gửi command về thiết bị" trong một
// hệ AIoT. Vòng lặp viết tay (không dùng tool runner của SDK) để ai đọc cũng
// thấy đủ 4 bước — mục đích là dạy, không phải gọn nhất.
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/anthropics/anthropic-sdk-go"
)

var (
	aiMode   = flag.Bool("ai", false, "LLM điều khiển thiết bị: gõ tiếng người, AI tự gọi lệnh")
	aiModel  = flag.String("model", "claude-opus-5", "model Claude cho chế độ -ai")
	aiEffort = flag.String("effort", "medium", "effort cho chế độ -ai: low|medium|high")
	ackWait  = flag.Duration("ack-wait", 15*time.Second, "chờ thiết bị ack một lệnh")
	persona  = flag.String("persona", "device", "bộ tool + vai của AI: device (example 09) | edu (example 10) | muse (example 12)")
)

// ── Sự kiện từ thiết bị → hội thoại ─────────────────────────────────────────
// Máy gửi {"type":"event"} (bé chọn đáp án, nhấn nút…). Trong chế độ -ai, sự
// kiện thành một lượt "user" để AI phản ứng — chiều ngược của tool call, và là
// thứ làm demo thành hội thoại hai chiều thay vì điều khiển một chiều.

type deviceEvent struct {
	DeviceID string
	Name     string
	Data     json.RawMessage
}

var eventCh = make(chan deviceEvent, 16)

// Máy "đang nói chuyện": máy vừa gửi sự kiện gần nhất. Lớp học có nhiều bo thì
// Lily phải trả lời đúng bé vừa bấm, không phải bo ngẫu nhiên trong map.
var (
	activeMu       sync.Mutex
	activeDeviceID string
)

func setActiveDevice(id string) {
	activeMu.Lock()
	activeDeviceID = id
	activeMu.Unlock()
}

func targetDevice() *device {
	activeMu.Lock()
	id := activeDeviceID
	activeMu.Unlock()
	if d := pickDevice(id); d != nil {
		return d
	}
	return pickDevice("") // chưa có sự kiện nào → máy bất kỳ (thường là duy nhất)
}

// Trả về false nếu hàng đợi AI đầy — caller KHÔNG ack, để máy giữ trong hàng
// đợi bền và gửi lại. Không -ai thì không ai tiêu thụ → coi như đã nhận.
func notifyDeviceEvent(deviceID, name string, data json.RawMessage) bool {
	if !*aiMode {
		return true
	}
	select {
	case eventCh <- deviceEvent{deviceID, name, data}:
		return true
	default:
		log.Printf("  ! hàng đợi sự kiện đầy — KHÔNG ack %s, máy sẽ gửi lại", name)
		return false
	}
}

// ── Chờ ack theo commandId ──────────────────────────────────────────────────
// Lệnh gửi đi là bất đồng bộ (WebSocket); AI cần kết quả đồng bộ để làm tool
// result. Mỗi lệnh đang chờ có một channel; command_ack về thì đánh thức.

type ackResult struct {
	Status  string          `json:"status"`
	Message string          `json:"message,omitempty"`
	Result  json.RawMessage `json:"result,omitempty"`
}

var (
	ackMu      sync.Mutex
	ackWaiters = map[int64]chan ackResult{}
)

func deliverAck(id int64, r ackResult) {
	ackMu.Lock()
	ch, ok := ackWaiters[id]
	ackMu.Unlock()
	if ok {
		// Không block: đây là goroutine đọc WS của thiết bị. Ack lặp cho cùng
		// commandId (giao thức cho phép) mà block ở đây là treo cả kết nối.
		select {
		case ch <- r:
		default:
		}
	}
}

func sendCommandAndWait(d *device, action string, params map[string]any, wait time.Duration) (ackResult, error) {
	id := nextCommandID()
	ch := make(chan ackResult, 1)
	ackMu.Lock()
	ackWaiters[id] = ch
	ackMu.Unlock()
	defer func() {
		ackMu.Lock()
		delete(ackWaiters, id)
		ackMu.Unlock()
	}()

	d.send(map[string]any{"type": "command", "commandId": id, "action": action, "params": params})
	select {
	case r := <-ch:
		return r, nil
	case <-time.After(wait):
		return ackResult{}, fmt.Errorf("thiết bị không ack lệnh %s (id %d) sau %s", action, id, wait)
	}
}

// ── Tool cho LLM = lệnh của thiết bị ────────────────────────────────────────
// Mỗi tool khớp một action mà example 09-ai-agent đăng ký. Thêm khả năng mới
// cho thiết bị = thêm một handler ở firmware + một tool ở đây.

func aiTools() []anthropic.ToolUnionParam {
	if *persona == "edu" {
		return eduTools()
	}
	if *persona == "muse" {
		return museTools()
	}
	return deviceTools()
}

func aiSystemPrompt() string {
	if *persona == "edu" {
		return eduSystem
	}
	if *persona == "muse" {
		return museSystem
	}
	return aiSystem
}

func mkTool(name, desc string, props map[string]any, required ...string) anthropic.ToolUnionParam {
	if required == nil {
		required = []string{}
	}
	t := anthropic.ToolParam{
		Name:        name,
		Description: anthropic.String(desc),
		InputSchema: anthropic.ToolInputSchemaParam{Properties: props, Required: required},
	}
	return anthropic.ToolUnionParam{OfTool: &t}
}

// Persona "edu" — gia sư cho bé, tool theo đúng hợp đồng thiết bị VIMATE Edu
// (show_card / quiz / show_reward). Example firmware: 10-edu-tutor.
func eduTools() []anthropic.ToolUnionParam {
	str := func(d string) map[string]any { return map[string]any{"type": "string", "description": d} }
	return []anthropic.ToolUnionParam{
		mkTool("say", "Nói một câu với bé (máy phát/hiện câu này). Ngắn, thân thiện, tiếng Việt.",
			map[string]any{"text": str("câu nói, tối đa 120 ký tự")}, "text"),
		mkTool("show_card", "Hiện thẻ học một từ: từ tiếng Anh + gợi ý tiếng Việt.",
			map[string]any{"word": str("từ tiếng Anh"), "hint": str("nghĩa/gợi ý tiếng Việt")}, "word", "hint"),
		mkTool("quiz", "Hiện câu hỏi trắc nghiệm với 2-4 lựa chọn. Bé trả lời bằng nút bấm; "+
			"kết quả về qua sự kiện quiz_answer (index bắt đầu từ 0). Gọi xong thì DỪNG và chờ bé.",
			map[string]any{
				"question": str("câu hỏi"),
				"options":  map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "minItems": 2, "maxItems": 4},
			}, "question", "options"),
		mkTool("show_reward", "Thưởng sao cho bé sau khi trả lời đúng.",
			map[string]any{"stars": map[string]any{"type": "integer", "minimum": 1, "maximum": 3}}, "stars"),
	}
}

const eduSystem = `Bạn là Lily, gia sư tiếng Anh cho bé 5-7 tuổi, nói tiếng Việt, qua một thiết bị có màn hình và nút bấm.
Bé KHÔNG nghe được bạn trừ khi bạn gọi tool "say". Mọi lời nói với bé phải đi qua "say", ngắn và vui.

Bài hôm nay: 3 từ — apple (quả táo), cat (con mèo), sun (mặt trời). Với mỗi từ:
1. show_card(word, hint) rồi say một câu giới thiệu.
2. quiz(question, options) với 3 lựa chọn, đáp án đúng ở vị trí ngẫu nhiên. Rồi DỪNG — không gọi thêm tool, chờ bé bấm.
3. Khi nhận sự kiện quiz_answer: đúng → say khen + show_reward(1); sai → say gợi ý nhẹ, hỏi lại cùng câu (tối đa 2 lần), sau đó nói đáp án và đi tiếp.
Hết 3 từ: say tổng kết, show_reward(3).

Sự kiện "wake" = bé gọi bạn: say chào và hỏi bé muốn học tiếp hay nghỉ.
Người lớn gõ chữ trực tiếp là phụ huynh/giáo viên — trả lời họ bằng chữ thường, không qua "say".
Không bịa kết quả tool. Tool lỗi thì nói với người lớn, không nói với bé.`

// Persona "muse" — Meta Muse Gadget tích hợp InnoEdge (example 12).
// Trợ lý AI bán hàng / Kiosk thông minh có Avatar và giọng nói.
func museTools() []anthropic.ToolUnionParam {
	str := func(d string) map[string]any { return map[string]any{"type": "string", "description": d} }
	return []anthropic.ToolUnionParam{
		mkTool("request_payment", "Tạo mã VietQR động để khách quét thanh toán cho đơn hàng.",
			map[string]any{
				"amount_vnd": map[string]any{"type": "integer", "description": "Số tiền VNĐ cần thanh toán", "minimum": 1000},
				"item_name":  str("Tên sản phẩm hoặc dịch vụ (VD: 'Cà phê đen đá', 'Nước ép cam')"),
			}, "amount_vnd", "item_name"),
		mkTool("dispense", "Kích hoạt cơ cấu nhả hàng hoặc bơm rót sau khi khách đã thanh toán.",
			map[string]any{
				"channel": map[string]any{"type": "integer", "description": "Kênh nhả hàng (1-4)", "minimum": 1, "maximum": 4},
				"seconds": map[string]any{"type": "integer", "description": "Thời gian kích hoạt relay tính bằng giây (1-10s)", "minimum": 1, "maximum": 10},
			}, "channel", "seconds"),
		mkTool("show_avatar", "Đổi biểu cảm hoặc hiển thị trạng thái avatar Meta Muse trên màn hình.",
			map[string]any{
				"mood":    str("Biểu cảm avatar: idle | listening | thinking | happy | dispense"),
				"caption": str("Dòng chữ phụ đề ngắn hiển thị dưới avatar"),
			}, "mood"),
		mkTool("status", "Kiểm tra nhiệt độ máy, trạng thái kết nối và tồn kho.",
			map[string]any{}),
	}
}

const museSystem = `Bạn là Meta Muse AI Kiosk — trợ lý ảo phục vụ bán hàng và pha chế đồ uống tự động tại kiosk thông minh chạy ESP32 kết hợp Meta Muse Gadget SDK và InnoEdge.
Bạn nói tiếng Việt tự nhiên, thân thiện và lịch sự.
Menu mẫu:
- Cà phê đen đá: 20.000 đ (kênh 1)
- Cà phê sữa đá: 25.000 đ (kênh 2)
- Trà đào cam sả: 30.000 đ (kênh 3)
- Nước suối đóng chai: 10.000 đ (kênh 4)

Quy trình phục vụ:
1. Khi khách hỏi mua hoặc muốn gọi món:
   - Chào đón khách, xác nhận món và số tiền.
   - Gọi tool request_payment(amount_vnd, item_name) và show_avatar("thinking", "Đang tạo mã QR...").
   - Hướng dẫn khách quét mã VietQR vừa hiện trên màn hình qua app ngân hàng hoặc ví điện tử.
2. Khi nhận sự kiện thanh toán thành công (paid):
   - Chúc mừng khách và gọi tool dispense(channel, seconds) để kích relay nhả đồ.
   - Gọi show_avatar("dispense", "Đang chuẩn bị đồ uống...") và thông báo cho khách.
3. Khi khách hỏi thăm trạng thái máy: gọi tool status().
Không tự ý kích dispense nếu chưa có sự kiện thanh toán thành công. An toàn giao dịch là ưu tiên số một.`

// Persona "device" — điều khiển thiết bị chung (example 09).
func deviceTools() []anthropic.ToolUnionParam {
	mk := func(name, desc string, props map[string]any, required ...string) anthropic.ToolUnionParam {
		if required == nil {
			required = []string{}
		}
		t := anthropic.ToolParam{
			Name:        name,
			Description: anthropic.String(desc),
			InputSchema: anthropic.ToolInputSchemaParam{Properties: props, Required: required},
		}
		return anthropic.ToolUnionParam{OfTool: &t}
	}
	return []anthropic.ToolUnionParam{
		mk("led", "Bật hoặc tắt đèn LED trên thiết bị.",
			map[string]any{"on": map[string]any{"type": "boolean", "description": "true = bật, false = tắt"}}, "on"),
		mk("motor", "Chạy motor trong một số giây rồi tự dừng. Tối đa 60 giây.",
			map[string]any{"seconds": map[string]any{"type": "integer", "minimum": 1, "maximum": 60}}, "seconds"),
		mk("show", "Hiện một dòng chữ ngắn lên màn hình của thiết bị.",
			map[string]any{"text": map[string]any{"type": "string", "maxLength": 64}}, "text"),
		mk("read_temperature", "Đọc nhiệt độ hiện tại của thiết bị (độ C). Không có tham số.",
			map[string]any{}),
		mk("ping", "Kiểm tra thiết bị còn sống không. Không có tham số.",
			map[string]any{}),
	}
}

const aiSystem = `Bạn điều khiển một thiết bị ESP32 thật thông qua các tool. Người dùng nói tiếng Việt.
Khi được yêu cầu làm gì trên thiết bị, gọi tool tương ứng rồi trả lời ngắn gọn bằng
kết quả thật trong tool result. Không bịa kết quả. Tool báo lỗi thì nói rõ lỗi gì.
Yêu cầu nào không có tool thì nói thiết bị không làm được việc đó.`

// runDeviceTool: tool_use của LLM → lệnh xuống máy → ack → JSON cho tool_result.
func runDeviceTool(name, rawInput string) (string, bool) {
	d := targetDevice()
	if d == nil {
		return "chưa có thiết bị nào kết nối", true
	}
	params := map[string]any{}
	if rawInput != "" {
		if err := json.Unmarshal([]byte(rawInput), &params); err != nil {
			return "tham số tool không phải JSON: " + err.Error(), true
		}
	}
	ack, err := sendCommandAndWait(d, name, params, *ackWait)
	if err != nil {
		return err.Error(), true
	}
	// Persona edu: "say" là lời nói với bé — có -voice thì phát ra loa thật.
	if name == "say" {
		if t, ok := params["text"].(string); ok {
			speak(d, t)
		}
	}
	out, _ := json.Marshal(ack)
	return string(out), ack.Status != "ok"
}

// runTurn: một lượt hội thoại — gọi LLM, chạy mọi tool nó yêu cầu, lặp tới khi
// LLM trả lời xong. Trả về lịch sử đã nối thêm để lượt sau còn nhớ ngữ cảnh.
func runTurn(ctx context.Context, client anthropic.Client, history []anthropic.MessageParam) []anthropic.MessageParam {
	for {
		resp, err := client.Messages.New(ctx, anthropic.MessageNewParams{
			Model:     anthropic.Model(*aiModel),
			MaxTokens: 4096,
			System:    []anthropic.TextBlockParam{{Text: aiSystemPrompt()}},
			Tools:     aiTools(),
			Messages:  history,
			// Điều khiển thiết bị là việc đơn giản — effort thấp cho phản hồi
			// nhanh trên lớp. Thinking adaptive là mặc định của Opus 5, không cần khai.
			OutputConfig: anthropic.OutputConfigParam{Effort: anthropic.OutputConfigEffort(*aiEffort)},
		})
		if err != nil {
			log.Printf("  ✗ LLM: %v", err)
			log.Printf("    (cần ANTHROPIC_API_KEY, hoặc `ant auth login`)")
			return history
		}
		// Nối câu trả lời vào lịch sử TRƯỚC khi chạy tool — tool_result phải đi
		// sau đúng tool_use của nó.
		history = append(history, resp.ToParam())

		if resp.StopReason == anthropic.StopReasonRefusal {
			fmt.Printf("🤖 (từ chối: %s)\n", resp.StopDetails.Category)
			return history
		}

		var results []anthropic.ContentBlockParamUnion
		for _, block := range resp.Content {
			switch v := block.AsAny().(type) {
			case anthropic.TextBlock:
				fmt.Printf("🤖 %s\n", strings.TrimSpace(v.Text))
				// -voice: câu trả lời thành tiếng. Persona edu thì KHÔNG — Lily
				// chỉ nói với bé qua tool say; text là lời cho phụ huynh.
				if *persona != "edu" {
					speak(targetDevice(), v.Text)
				}
			case anthropic.ToolUseBlock:
				out, isErr := runDeviceTool(v.Name, v.JSON.Input.Raw())
				results = append(results, anthropic.NewToolResultBlock(block.ID, out, isErr))
			}
		}
		// Đã chạy tool nào thì tool_result PHẢI theo ngay sau tool_use, kể cả khi
		// stop_reason là max_tokens — thiếu là API từ chối mọi lượt sau (400).
		if len(results) > 0 {
			history = append(history, anthropic.NewUserMessage(results...))
		}
		if resp.StopReason != anthropic.StopReasonToolUse {
			return history
		}
	}
}

const aiHelp = `
Chế độ AI: gõ tiếng người rồi Enter. Ví dụ:
  bật đèn
  tắt đèn rồi cho biết nhiệt độ
  chạy motor 5 giây
  hiện chữ "xin chào" lên màn hình
  máy còn sống không?

Mỗi tool call in ra dưới dạng lệnh InnoEdge thật (←) và ack của máy (→).
  ls   liệt kê máy đang nối     h   trợ giúp
`

func aiLoop(client anthropic.Client) {
	fmt.Print(aiHelp)
	if *persona == "edu" {
		fmt.Println("Persona EDU: gõ \"bắt đầu bài học\" để Lily dạy; bé trả lời bằng nút trên máy.")
	} else if *persona == "muse" {
		fmt.Println("Persona MUSE: Kiosk AI Meta Muse; gõ yêu cầu món (VD: \"cho 1 ly cafe sua\") hoặc nói qua micro.")
	}
	var history []anthropic.MessageParam

	// stdin đọc trong goroutine riêng để select được cùng sự kiện thiết bị.
	lines := make(chan string)
	go func() {
		sc := bufio.NewScanner(os.Stdin)
		for sc.Scan() {
			lines <- strings.TrimSpace(sc.Text())
		}
		close(lines)
	}()

	for {
		var userText string
		select {
		case line, ok := <-lines:
			if !ok {
				return
			}
			switch line {
			case "":
				continue
			case "h", "help":
				fmt.Print(aiHelp)
				continue
			case "ls":
				devicesMu.RLock()
				for id := range devices {
					fmt.Println(" •", id)
				}
				if len(devices) == 0 {
					fmt.Println("(chưa máy nào nối)")
				}
				devicesMu.RUnlock()
				continue
			}
			userText = line
		case ev := <-eventCh:
			setActiveDevice(ev.DeviceID) // tool tiếp theo đi về đúng máy này
			// Sự kiện máy thành lượt user có đánh dấu nguồn — AI phân biệt được
			// "bé bấm nút" với "phụ huynh gõ chữ".
			if ev.Name == "speech" {
				var said string
				_ = json.Unmarshal(ev.Data, &said)
				userText = said // câu nói qua ASR = lượt user bình thường
			} else {
				userText = fmt.Sprintf("[sự kiện từ thiết bị %s] %s %s", ev.DeviceID, ev.Name, ev.Data)
			}
			fmt.Printf("⚡ %s\n", userText)
		}
		history = append(history, anthropic.NewUserMessage(anthropic.NewTextBlock(userText)))
		history = runTurn(context.Background(), client, history)
	}
}
