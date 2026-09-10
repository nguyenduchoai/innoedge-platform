// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Test chế độ -ai KHÔNG cần API key: giả lập cả LLM (HTTP) lẫn thiết bị (WS),
// kiểm tra vòng lặp người → LLM → lệnh → ESP32 → ack → LLM đi đúng đường.
package main

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/anthropics/anthropic-sdk-go"
	"github.com/anthropics/anthropic-sdk-go/option"
)

// fakeLLM đóng vai POST /v1/messages: lần 1 yêu cầu gọi tool led(on), lần 2
// trả lời xong. Ghi lại body lần 2 để kiểm tra tool_result đã về đúng.
func fakeLLM(t *testing.T) (*httptest.Server, *[]byte) {
	t.Helper()
	var calls int32
	var second []byte
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		n := atomic.AddInt32(&calls, 1)
		w.Header().Set("Content-Type", "application/json")
		if n == 1 {
			_, _ = w.Write([]byte(`{"id":"msg_1","type":"message","role":"assistant","model":"claude-opus-5",
			  "content":[{"type":"tool_use","id":"toolu_1","name":"led","input":{"on":true}}],
			  "stop_reason":"tool_use","usage":{"input_tokens":1,"output_tokens":1}}`))
			return
		}
		second = body
		_, _ = w.Write([]byte(`{"id":"msg_2","type":"message","role":"assistant","model":"claude-opus-5",
		  "content":[{"type":"text","text":"Đã bật đèn."}],
		  "stop_reason":"end_turn","usage":{"input_tokens":1,"output_tokens":1}}`))
	}))
	t.Cleanup(srv.Close)
	return srv, &second
}

func TestAIVongLapLenhVaAck(t *testing.T) {
	llm, secondBody := fakeLLM(t)
	cloud := httptest.NewServer(newMux())
	defer cloud.Close()

	// Thiết bị giả: nối vào, chào, rồi ack mọi lệnh "led" với status ok.
	dev := dial(t, cloud)
	_ = dev.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, dev, "hello")
	gotAction := make(chan string, 1)
	go func() {
		for {
			var m map[string]any
			if err := dev.ReadJSON(&m); err != nil {
				return
			}
			if m["type"] == "command" {
				gotAction <- m["action"].(string)
				_ = dev.WriteJSON(map[string]any{
					"type": "command_ack", "commandId": m["commandId"],
					"status": "ok", "message": "LED da bat", "result": map[string]any{"on": true},
				})
			}
		}
	}()
	// Chờ mock đăng ký device vào bảng (handleWS chạy trong goroutine riêng).
	deadline := time.Now().Add(2 * time.Second)
	for pickDevice("") == nil && time.Now().Before(deadline) {
		time.Sleep(10 * time.Millisecond)
	}

	client := anthropic.NewClient(option.WithBaseURL(llm.URL), option.WithAPIKey("test-key"))
	history := []anthropic.MessageParam{anthropic.NewUserMessage(anthropic.NewTextBlock("bật đèn"))}
	history = runTurn(context.Background(), client, history)

	// 1. Lệnh thật đã xuống thiết bị.
	select {
	case a := <-gotAction:
		if a != "led" {
			t.Errorf("thiết bị nhận action %q, mong led", a)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("thiết bị không nhận được lệnh nào")
	}

	// 2. Ack của thiết bị đã quay về LLM dưới dạng tool_result.
	if *secondBody == nil {
		t.Fatal("LLM không được gọi lần 2 — vòng lặp dừng sớm")
	}
	var req struct {
		Messages []struct {
			Role    string          `json:"role"`
			Content json.RawMessage `json:"content"`
		} `json:"messages"`
	}
	if err := json.Unmarshal(*secondBody, &req); err != nil {
		t.Fatal(err)
	}
	last := string(req.Messages[len(req.Messages)-1].Content)
	if !strings.Contains(last, `"tool_result"`) || !strings.Contains(last, "toolu_1") {
		t.Errorf("message cuối phải là tool_result cho toolu_1, đang là: %.200s", last)
	}
	if !strings.Contains(last, "LED da bat") {
		t.Error("tool_result phải chứa message ack THẬT của thiết bị, không phải giả định")
	}

	// 3. Lịch sử: user → assistant(tool_use) → user(tool_result) → assistant(text).
	if len(history) != 4 {
		t.Errorf("lịch sử có %d message, mong 4", len(history))
	}
}

func TestAIKhongCoThietBiThiBaoLoiChoLLM(t *testing.T) {
	// Không thiết bị nào nối → tool phải trả is_error để LLM nói thật với người dùng.
	devicesMu.Lock()
	for k := range devices {
		delete(devices, k)
	}
	devicesMu.Unlock()
	out, isErr := runDeviceTool("led", `{"on":true}`)
	if !isErr || !strings.Contains(out, "chưa có thiết bị") {
		t.Errorf("mong is_error + thông báo rõ, nhận (%q, %v)", out, isErr)
	}
}

func TestAIThietBiKhongAckThiTimeout(t *testing.T) {
	cloud := httptest.NewServer(newMux())
	defer cloud.Close()
	dev := dial(t, cloud) // nối nhưng KHÔNG bao giờ ack
	_ = dev.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, dev, "hello")
	deadline := time.Now().Add(2 * time.Second)
	for pickDevice("") == nil && time.Now().Before(deadline) {
		time.Sleep(10 * time.Millisecond)
	}
	old := *ackWait
	*ackWait = 200 * time.Millisecond
	defer func() { *ackWait = old }()

	out, isErr := runDeviceTool("ping", "")
	if !isErr || !strings.Contains(out, "không ack") {
		t.Errorf("máy im lặng phải thành lỗi có lý do, nhận (%q, %v)", out, isErr)
	}
}
