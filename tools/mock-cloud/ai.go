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
)

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
		ch <- r
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
	d := pickDevice("")
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
			System:    []anthropic.TextBlockParam{{Text: aiSystem}},
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
			case anthropic.ToolUseBlock:
				out, isErr := runDeviceTool(v.Name, v.JSON.Input.Raw())
				results = append(results, anthropic.NewToolResultBlock(block.ID, out, isErr))
			}
		}
		if resp.StopReason != anthropic.StopReasonToolUse {
			return history
		}
		// Mọi tool_result của một lượt đi chung MỘT user message.
		history = append(history, anthropic.NewUserMessage(results...))
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
	var history []anthropic.MessageParam
	sc := bufio.NewScanner(os.Stdin)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
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
		history = append(history, anthropic.NewUserMessage(anthropic.NewTextBlock(line)))
		history = runTurn(context.Background(), client, history)
	}
}
