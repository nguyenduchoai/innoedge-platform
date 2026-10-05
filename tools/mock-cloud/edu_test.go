// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

package main

import (
	"encoding/json"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// Sự kiện tuỳ ý (v1.1): mock phải ack theo seq — không ack là hàng đợi bền của
// máy gửi lại mãi.
func TestEventDuocAckTheoSeq(t *testing.T) {
	old := *aiMode
	*aiMode = false
	defer func() { *aiMode = old }()

	srv := httptest.NewServer(newMux())
	defer srv.Close()
	c := dial(t, srv)
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	_ = c.WriteJSON(map[string]any{
		"type": "event", "name": "quiz_answer", "seq": 77, "data": map[string]any{"index": 2},
	})
	ack := readUntil(t, c, "event_ack")
	if num(t, ack, "seq") != 77 {
		t.Errorf("event_ack seq = %v, mong 77", ack["seq"])
	}
}

// Chế độ -ai: sự kiện máy phải thành một lượt hội thoại có đánh dấu nguồn,
// để AI phân biệt "bé bấm nút" với "phụ huynh gõ chữ".
func TestEventVaoHoiThoaiKhiAI(t *testing.T) {
	old := *aiMode
	*aiMode = true
	defer func() { *aiMode = old }()
	// xả hàng đợi cũ
	for len(eventCh) > 0 {
		<-eventCh
	}

	notifyDeviceEvent("AABB", "quiz_answer", json.RawMessage(`{"index":1}`))
	select {
	case ev := <-eventCh:
		if ev.Name != "quiz_answer" || ev.DeviceID != "AABB" {
			t.Errorf("sự kiện sai: %+v", ev)
		}
		if !strings.Contains(string(ev.Data), `"index":1`) {
			t.Errorf("data bị mất: %s", ev.Data)
		}
	case <-time.After(time.Second):
		t.Fatal("sự kiện không tới hàng đợi AI")
	}
}

// Không -ai thì sự kiện KHÔNG được xếp hàng — không có ai tiêu thụ, hàng đợi
// đầy rồi bỏ rơi sự kiện thật lúc bật -ai sau.
func TestEventKhongXepHangKhiKhongAI(t *testing.T) {
	old := *aiMode
	*aiMode = false
	defer func() { *aiMode = old }()
	for len(eventCh) > 0 {
		<-eventCh
	}
	notifyDeviceEvent("AABB", "wake", nil)
	if len(eventCh) != 0 {
		t.Error("không -ai mà vẫn xếp hàng sự kiện")
	}
}

// Persona edu: tool đúng hợp đồng VIMATE (say/show_card/quiz/show_reward) và
// system prompt bắt AI nói với bé qua "say", dừng sau quiz.
func TestPersonaEduToolVaPrompt(t *testing.T) {
	old := *persona
	*persona = "edu"
	defer func() { *persona = old }()

	names := map[string]bool{}
	for _, tu := range aiTools() {
		names[tu.OfTool.Name] = true
	}
	for _, want := range []string{"say", "show_card", "quiz", "show_reward"} {
		if !names[want] {
			t.Errorf("persona edu thiếu tool %s", want)
		}
	}
	if names["motor"] {
		t.Error("persona edu không được có tool motor")
	}
	sys := aiSystemPrompt()
	for _, k := range []string{"say", "quiz_answer", "DỪNG", "wake"} {
		if !strings.Contains(sys, k) {
			t.Errorf("system prompt edu thiếu %q", k)
		}
	}
}

func TestPersonaMacDinhLaDevice(t *testing.T) {
	names := map[string]bool{}
	for _, tu := range aiTools() {
		names[tu.OfTool.Name] = true
	}
	if !names["led"] || !names["motor"] || names["quiz"] {
		t.Errorf("persona device sai bộ tool: %v", names)
	}
}

func TestPersonaMuseToolVaPrompt(t *testing.T) {
	old := *persona
	*persona = "muse"
	defer func() { *persona = old }()

	names := map[string]bool{}
	for _, tu := range aiTools() {
		names[tu.OfTool.Name] = true
	}
	for _, want := range []string{"request_payment", "dispense", "show_avatar", "status"} {
		if !names[want] {
			t.Errorf("persona muse thiếu tool %s", want)
		}
	}
	if names["quiz"] || names["say"] {
		t.Error("persona muse không được có tool quiz hay say của edu")
	}
	sys := aiSystemPrompt()
	for _, k := range []string{"Meta Muse", "VietQR", "paid", "dispense", "show_avatar"} {
		if !strings.Contains(sys, k) {
			t.Errorf("system prompt muse thiếu %q", k)
		}
	}
}
