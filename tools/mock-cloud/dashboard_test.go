// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestDashboardHTMLAndAPIDevices(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()

	// 1. Kiểm tra GET /dashboard trả về HTML 200
	resp, err := http.Get(srv.URL + "/dashboard")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Errorf("GET /dashboard mong 200, nhận %d", resp.StatusCode)
	}

	// 2. Cho một thiết bị giả kết nối vào qua WebSocket
	c := dialAs(t, srv, "DASH00112233")
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	// Gửi heartbeat
	_ = c.WriteJSON(map[string]any{
		"type": "heartbeat", "fw_version": "0.1.2", "rssi": -65, "queue_depth": 3,
	})
	time.Sleep(50 * time.Millisecond)

	// 3. Kiểm tra GET /api/devices
	respDev, err := http.Get(srv.URL + "/api/devices")
	if err != nil {
		t.Fatal(err)
	}
	defer respDev.Body.Close()
	var devBody struct {
		Status  string        `json:"status"`
		Devices []DeviceStats `json:"devices"`
	}
	if err := json.NewDecoder(respDev.Body).Decode(&devBody); err != nil {
		t.Fatal(err)
	}
	if devBody.Status != "ok" {
		t.Errorf("status = %s, mong ok", devBody.Status)
	}
	found := false
	for _, d := range devBody.Devices {
		if d.ID == "DASH00112233" {
			found = true
			if !d.Online {
				t.Error("thiết bị phải online")
			}
			if d.FWVersion != "0.1.2" {
				t.Errorf("fwVersion = %s, mong 0.1.2", d.FWVersion)
			}
			if d.RSSI != -65 {
				t.Errorf("rssi = %d, mong -65", d.RSSI)
			}
		}
	}
	if !found {
		t.Error("không tìm thấy thiết bị DASH00112233 trong danh sách api/devices")
	}
}

func TestAPIDeviceCommandVaDup(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()

	c := dialAs(t, srv, "CMDTEST0001")
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	// Gửi lệnh qua REST API: POST /api/devices/command
	payload := []byte(`{"deviceId":"CMDTEST0001","action":"dispense","params":{"amountVnd":20000}}`)
	resp, err := http.Post(srv.URL+"/api/devices/command", "application/json", bytes.NewReader(payload))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("POST /api/devices/command trả %d", resp.StatusCode)
	}
	var res map[string]any
	_ = json.NewDecoder(resp.Body).Decode(&res)
	if res["status"] != "ok" || res["action"] != "dispense" {
		t.Errorf("kết quả command lạ: %v", res)
	}

	// Đọc trên WebSocket xem thiết bị có nhận được frame command không
	frame := readUntil(t, c, "command")
	if frame["action"] != "dispense" {
		t.Errorf("action = %v, mong dispense", frame["action"])
	}

	// Test POST /api/devices/dup để kiểm tra tính năng gửi lại (idempotency test)
	respDup, err := http.Post(srv.URL+"/api/devices/dup", "application/json", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer respDup.Body.Close()
	if respDup.StatusCode != http.StatusOK {
		t.Fatalf("POST /api/devices/dup trả %d", respDup.StatusCode)
	}

	// Thiết bị phải nhận được CÙNG commandId
	dupFrame := readUntil(t, c, "command")
	if num(t, dupFrame, "commandId") != num(t, frame, "commandId") {
		t.Errorf("dup phải cùng commandId, gốc=%v, dup=%v", frame["commandId"], dupFrame["commandId"])
	}
}

func TestWebhooksSepayVaPayOS(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()

	c := dialAs(t, srv, "PAYTEST0001")
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	// 1. Test Webhook SePAY
	sepayJSON := []byte(`{
		"id": 99991,
		"gateway": "MBBank",
		"transactionDate": "2026-10-04 18:00:00",
		"accountNumber": "0987654321",
		"content": "Thanh toan GTMOCKD00088",
		"transferType": "in",
		"transferAmount": 20000,
		"referenceCode": "MB.123456"
	}`)
	resp, err := http.Post(srv.URL+"/api/webhook/sepay", "application/json", bytes.NewReader(sepayJSON))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("webhook sepay trả %d", resp.StatusCode)
	}

	paidFrame := readUntil(t, c, "payment_paid")
	if num(t, paidFrame, "amount") != 20000 {
		t.Errorf("amount = %v, mong 20000", paidFrame["amount"])
	}
	if paidFrame["refCode"] != "GTMOCKD00088" {
		t.Errorf("refCode = %v, mong GTMOCKD00088", paidFrame["refCode"])
	}

	// 2. Test Webhook PayOS
	payosJSON := []byte(`{
		"code": "00",
		"desc": "success",
		"data": {
			"orderCode": 777,
			"amount": 50000,
			"description": "GTMOCKD00077 thanh toan",
			"reference": "PAYOS_REF_01"
		}
	}`)
	respPayOS, err := http.Post(srv.URL+"/api/webhook/payos", "application/json", bytes.NewReader(payosJSON))
	if err != nil {
		t.Fatal(err)
	}
	defer respPayOS.Body.Close()
	if respPayOS.StatusCode != http.StatusOK {
		t.Fatalf("webhook payos trả %d", respPayOS.StatusCode)
	}

	paidPayOS := readUntil(t, c, "payment_paid")
	if num(t, paidPayOS, "amount") != 50000 {
		t.Errorf("payos amount = %v, mong 50000", paidPayOS["amount"])
	}
	if num(t, paidPayOS, "intentId") != 777 {
		t.Errorf("payos intentId = %v, mong 777", paidPayOS["intentId"])
	}
}

func TestWebhooksPay2SVaTingee(t *testing.T) {
	srv := httptest.NewServer(newMux())
	defer srv.Close()

	c := dialAs(t, srv, "PAY2STEST001")
	_ = c.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, c, "hello")

	// 1. Test Webhook Pay2S (dạng mảng transactions)
	pay2sJSON := []byte(`{
		"transactions": [
			{
				"id": 8881,
				"gateway": "Techcombank",
				"transactionDate": "2026-10-04 19:00:00",
				"content": "NAP GTMOCKD00055",
				"transferAmount": 20000,
				"transferType": "IN"
			}
		]
	}`)
	respPay2s, err := http.Post(srv.URL+"/api/webhook/pay2s", "application/json", bytes.NewReader(pay2sJSON))
	if err != nil {
		t.Fatal(err)
	}
	defer respPay2s.Body.Close()
	if respPay2s.StatusCode != http.StatusOK {
		t.Fatalf("webhook pay2s trả %d", respPay2s.StatusCode)
	}

	paidPay2s := readUntil(t, c, "payment_paid")
	if num(t, paidPay2s, "amount") != 20000 {
		t.Errorf("pay2s amount = %v, mong 20000", paidPay2s["amount"])
	}
	if paidPay2s["refCode"] != "GTMOCKD00055" {
		t.Errorf("pay2s refCode = %v, mong GTMOCKD00055", paidPay2s["refCode"])
	}

	// 2. Test Webhook Tingee
	tingeeJSON := []byte(`{
		"requestId": "req-999",
		"orderId": "ORD-TINGEE-123",
		"amount": 30000,
		"paidAmount": 30000,
		"description": "Thanh toan GTMOCKD00066",
		"status": "success",
		"statusCode": "00"
	}`)
	respTingee, err := http.Post(srv.URL+"/api/webhook/tingee", "application/json", bytes.NewReader(tingeeJSON))
	if err != nil {
		t.Fatal(err)
	}
	defer respTingee.Body.Close()
	if respTingee.StatusCode != http.StatusOK {
		t.Fatalf("webhook tingee trả %d", respTingee.StatusCode)
	}

	paidTingee := readUntil(t, c, "payment_paid")
	if num(t, paidTingee, "amount") != 30000 {
		t.Errorf("tingee amount = %v, mong 30000", paidTingee["amount"])
	}
	if paidTingee["refCode"] != "GTMOCKD00066" {
		t.Errorf("tingee refCode = %v, mong GTMOCKD00066", paidTingee["refCode"])
	}
}
