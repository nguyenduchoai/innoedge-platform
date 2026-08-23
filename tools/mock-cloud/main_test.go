// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Test tích hợp: dựng mock-cloud thật rồi cho một "thiết bị" giả nối vào, gửi
// đúng các khung tin mà firmware gửi, và kiểm tra mock trả lời đúng spec trong
// docs/PROTOCOL-v1.md.
//
//	go test ./tools/mock-cloud
package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

// dial mở một phiên WebSocket giống hệt firmware: header Device-Id + Bearer.
func dial(t *testing.T, srv *httptest.Server) *websocket.Conn {
	t.Helper()
	u := "ws" + strings.TrimPrefix(srv.URL, "http") + "/ws/"
	conn, resp, err := websocket.DefaultDialer.Dial(u, http.Header{
		"Device-Id":     {"AABBCCDDEEFF"},
		"Authorization": {"Bearer factory-token-123456"},
	})
	if err != nil {
		t.Fatalf("không nối được %s: %v (resp=%v)", u, err, resp)
	}
	t.Cleanup(func() { conn.Close() })
	return conn
}

// readUntil đọc frame tới khi gặp type mong muốn (bỏ qua các frame khác).
func readUntil(t *testing.T, c *websocket.Conn, typ string) map[string]any {
	t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		_ = c.SetReadDeadline(deadline)
		_, data, err := c.ReadMessage()
		if err != nil {
			t.Fatalf("chờ %q nhưng đọc lỗi: %v", typ, err)
		}
		var m map[string]any
		if err := json.Unmarshal(data, &m); err != nil {
			t.Fatalf("mock trả JSON hỏng: %s", data)
		}
		if m["type"] == typ {
			return m
		}
	}
	t.Fatalf("quá hạn, không thấy frame %q", typ)
	return nil
}

func num(t *testing.T, m map[string]any, k string) int64 {
	t.Helper()
	f, ok := m[k].(float64)
	if !ok {
		t.Fatalf("thiếu trường số %q trong %v", k, m)
	}
	return int64(f)
}

func TestHandshakeVaKichHoat(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()
	c := dial(t, srv)

	if err := c.WriteJSON(map[string]any{
		"type": "hello", "transport": "websocket", "firmware": "0.1.0",
	}); err != nil {
		t.Fatal(err)
	}

	if h := readUntil(t, c, "hello"); h["session_id"] == "" {
		t.Error("hello trả về thiếu session_id")
	}

	// Mock tự kích hoạt máy — nếu không, example 07 (QR) sẽ kẹt ở "chưa gán".
	act := readUntil(t, c, "command")
	if act["command"] != "activation_complete" {
		t.Errorf("mong activation_complete, nhận %v", act["command"])
	}
	if tok, _ := act["authToken"].(string); tok == "" {
		t.Error("activation_complete phải kèm authToken để firmware lưu NVS")
	}

	if q := readUntil(t, c, "set_static_qr"); q["payload"] == "" {
		t.Error("set_static_qr thiếu payload")
	}
}

func TestAckTienDungDonGia(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()
	c := dial(t, srv)
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	// 3 xu × 1000đ = 3000đ. Sai chỗ này là báo cáo doanh thu sai.
	_ = c.WriteJSON(map[string]any{"type": "coin", "coins": 3, "seq": 42})
	ack := readUntil(t, c, "coin_ack")
	if ack["status"] != "ok" {
		t.Errorf("status = %v, mong ok", ack["status"])
	}
	if got := num(t, ack, "seq"); got != 42 {
		t.Errorf("seq = %d, mong 42 — firmware dùng seq này để xoá khỏi hàng đợi", got)
	}
	if got := num(t, ack, "amountVND"); got != 3000 {
		t.Errorf("amountVND = %d, mong 3000", got)
	}
	if got := num(t, ack, "rateVND"); got != 1000 {
		t.Errorf("rateVND = %d, mong 1000", got)
	}

	// Tiền mặt: amount là VND trực tiếp, KHÔNG nhân đơn giá.
	_ = c.WriteJSON(map[string]any{
		"type": "payment", "method": "cash", "amount": 20000, "seq": 43,
	})
	ack = readUntil(t, c, "coin_ack")
	if got := num(t, ack, "amountVND"); got != 20000 {
		t.Errorf("tiền mặt amountVND = %d, mong 20000", got)
	}
}

func TestQRVaWebhookGiaBaoDaTra(t *testing.T) {
	*paidAfter = 200 * time.Millisecond // đừng bắt test chờ 8 giây
	srv := httptest.NewServer(newMux())
	defer srv.Close()
	c := dial(t, srv)
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	_ = c.WriteJSON(map[string]any{"type": "qr_request", "amount": 20000, "seq": 44})
	qr := readUntil(t, c, "qr")
	for _, k := range []string{"intentId", "refCode", "amount", "qrPayload", "expiresSec"} {
		if _, ok := qr[k]; !ok {
			t.Errorf("frame qr thiếu %q — firmware đọc trường này", k)
		}
	}
	// Payload phải rõ ràng là giả: quét nhầm mà chuyển tiền thật là hỏng.
	if p, _ := qr["qrPayload"].(string); !strings.Contains(p, "MOCK") {
		t.Errorf("qrPayload phải nhìn là giả, đang là %q", p)
	}

	paid := readUntil(t, c, "payment_paid")
	if num(t, paid, "intentId") != num(t, qr, "intentId") {
		t.Error("payment_paid phải cùng intentId với frame qr")
	}
	if num(t, paid, "amount") != 20000 {
		t.Error("payment_paid sai số tiền")
	}
}

func TestConfigCoComboChoExample04Va08(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()

	req, _ := http.NewRequest("GET", srv.URL+"/api/device/config", nil)
	req.Header.Set("Device-Id", "AABBCCDDEEFF")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	var body struct {
		Status string `json:"status"`
		Data   struct {
			Version int    `json:"version"`
			Name    string `json:"name"`
			Config  struct {
				Combos []struct {
					ID      string `json:"id"`
					Payload struct {
						Steps []struct {
							Device  string `json:"device"`
							Seconds int    `json:"seconds"`
						} `json:"steps"`
					} `json:"payload"`
				} `json:"combos"`
			} `json:"config"`
		} `json:"data"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&body); err != nil {
		t.Fatal(err)
	}
	if body.Status != "ok" || body.Data.Version == 0 {
		t.Fatalf("shape sai: %+v", body)
	}
	if len(body.Data.Config.Combos) == 0 {
		t.Fatal("phải có combo, không thì example 08 không mở được phiên rửa")
	}
	// gtek_config_lookup_combo() đọc đúng hai trường này.
	s := body.Data.Config.Combos[0].Payload.Steps
	if len(s) == 0 || s[0].Device == "" || s[0].Seconds == 0 {
		t.Errorf("combo phải có payload.steps[].{device,seconds}, đang là %+v", s)
	}
}

func TestOTAMacDinhKhongCoBanMoi(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()

	req, _ := http.NewRequest("POST", srv.URL+"/ota/v1/", nil)
	req.Header.Set("Device-Id", "AABBCCDDEEFF")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()

	var body struct {
		Status string `json:"status"`
		Data   struct {
			Firmware   map[string]any `json:"firmware"`
			Assignment struct {
				Status string `json:"status"`
			} `json:"assignment"`
		} `json:"data"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&body); err != nil {
		t.Fatal(err)
	}
	if body.Status != "ok" {
		t.Errorf("status = %q", body.Status)
	}
	// Không đặt -fw thì KHÔNG được chào bản mới — chào rồi mà không tải được
	// (OTA bắt buộc https) chỉ làm máy thử đi thử lại vô ích.
	if body.Data.Firmware != nil {
		t.Errorf("mặc định không được chào firmware, đang trả %v", body.Data.Firmware)
	}
	if body.Data.Assignment.Status != "assigned" {
		t.Errorf("assignment.status = %q, mong assigned", body.Data.Assignment.Status)
	}
}
