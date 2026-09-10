// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Test hồi quy cho 4 lỗi mà /code-review tìm ra sau khi tác giả đã nói "xong".
// Mỗi test là một lỗi thật; xoá test = mời lỗi quay lại.
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

// Lỗi: deliverAck block goroutine đọc WS khi ack lặp. Sửa: gửi non-blocking.
func TestDeliverAckLapKhongTreo(t *testing.T) {
	id := nextCommandID()
	ch := make(chan ackResult, 1)
	ackMu.Lock()
	ackWaiters[id] = ch
	ackMu.Unlock()
	defer func() { ackMu.Lock(); delete(ackWaiters, id); ackMu.Unlock() }()

	done := make(chan struct{})
	go func() {
		deliverAck(id, ackResult{Status: "ok"})
		deliverAck(id, ackResult{Status: "ok"}) // ack thứ hai — trước đây treo ở đây
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("deliverAck treo khi ack lặp — goroutine đọc WS sẽ chết")
	}
}

// Lỗi: tool gửi tới máy ngẫu nhiên. Sửa: máy vừa gửi sự kiện là đích.
func TestToolDiVeDungMayVuaGuiSuKien(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()
	// hai máy nối vào
	dial2 := func(id string) (*websocketConn, chan string) {
		c := dialAs(t, srv, id)
		_ = c.WriteJSON(map[string]any{"type": "hello"})
		readUntil(t, c, "hello")
		got := make(chan string, 4)
		go func() {
			for {
				var m map[string]any
				if err := c.ReadJSON(&m); err != nil {
					return
				}
				if m["type"] == "command" {
					got <- id
					_ = c.WriteJSON(map[string]any{"type": "command_ack",
						"commandId": m["commandId"], "status": "ok"})
				}
			}
		}()
		return c, got
	}
	_, gotA := dial2("AAAA")
	_, gotB := dial2("BBBB")
	waitDevices(t, 2)

	setActiveDevice("BBBB") // bé ở máy B vừa bấm
	out, isErr := runDeviceTool("say", `{"text":"đúng rồi"}`)
	if isErr {
		t.Fatalf("tool lỗi: %s", out)
	}
	select {
	case <-gotB:
	case <-gotA:
		t.Fatal("lệnh đi tới máy A trong khi máy B vừa gửi sự kiện")
	case <-time.After(3 * time.Second):
		t.Fatal("không máy nào nhận lệnh")
	}
}

// Lỗi: tool_use không kèm tool_result khi stop_reason=max_tokens → 400 mãi.
// Sửa: đã chạy tool thì tool_result luôn được nối vào lịch sử.
func TestToolResultLuonDuocNoiKhiMaxTokens(t *testing.T) {
	var calls int32
	llm := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.ReadAll(r.Body)
		atomic.AddInt32(&calls, 1)
		w.Header().Set("Content-Type", "application/json")
		// tool_use nhưng stop_reason là max_tokens (bị cắt)
		_, _ = w.Write([]byte(`{"id":"m","type":"message","role":"assistant","model":"claude-opus-5",
		  "content":[{"type":"tool_use","id":"toolu_9","name":"ping","input":{}}],
		  "stop_reason":"max_tokens","usage":{"input_tokens":1,"output_tokens":1}}`))
	}))
	defer llm.Close()
	// không thiết bị → tool trả is_error, nhưng vẫn phải có tool_result
	devicesMu.Lock()
	for k := range devices {
		delete(devices, k)
	}
	devicesMu.Unlock()

	client := anthropic.NewClient(option.WithBaseURL(llm.URL), option.WithAPIKey("x"))
	h := runTurn(context.Background(), client,
		[]anthropic.MessageParam{anthropic.NewUserMessage(anthropic.NewTextBlock("ping"))})
	if atomic.LoadInt32(&calls) != 1 {
		t.Fatalf("max_tokens phải dừng vòng lặp sau 1 lần gọi, gọi %d", calls)
	}
	// user → assistant(tool_use) → user(tool_result)
	if len(h) != 3 {
		t.Fatalf("lịch sử %d message, mong 3 (tool_result phải được nối)", len(h))
	}
	b, _ := json.Marshal(h[2])
	if !strings.Contains(string(b), `"tool_result"`) || !strings.Contains(string(b), "toolu_9") {
		t.Errorf("message cuối phải là tool_result cho toolu_9: %s", b)
	}
}

// Lỗi: ack sự kiện trước khi vào hàng đợi AI → drop sau khi máy đã xoá.
// Sửa: đầy thì KHÔNG ack, máy giữ và gửi lại.
func TestSuKienDayThiKhongAck(t *testing.T) {
	old := *aiMode
	*aiMode = true
	defer func() { *aiMode = old }()
	for len(eventCh) > 0 {
		<-eventCh
	}
	for i := 0; i < cap(eventCh); i++ { // lấp đầy
		eventCh <- deviceEvent{"X", "wake", nil}
	}
	defer func() {
		for len(eventCh) > 0 {
			<-eventCh
		}
	}()

	srv := httptest.NewServer(newMux())
	defer srv.Close()
	c := dial(t, srv)
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")
	_ = c.WriteJSON(map[string]any{"type": "event", "name": "quiz_answer", "seq": 99, "data": map[string]any{"index": 0}})

	_ = c.SetReadDeadline(time.Now().Add(700 * time.Millisecond))
	for {
		var m map[string]any
		if err := c.ReadJSON(&m); err != nil {
			return // hết hạn mà không thấy event_ack = đúng
		}
		if m["type"] == "event_ack" {
			t.Fatal("ack sự kiện trong khi hàng đợi AI đầy — máy sẽ xoá khỏi hàng đợi bền và mất câu trả lời")
		}
	}
}
