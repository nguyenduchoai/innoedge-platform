// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"sync"
	"time"
)

// ── Models ──────────────────────────────────────────────────────────────────

type DeviceStats struct {
	ID          string    `json:"id"`
	Online      bool      `json:"online"`
	ConnectedAt time.Time `json:"connectedAt"`
	LastSeen    time.Time `json:"lastSeen"`
	FWVersion   string    `json:"fwVersion"`
	RSSI        int       `json:"rssi"`
	QueueDepth  int       `json:"queueDepth"`
	TotalCoins  int       `json:"totalCoins"`
	TotalVND    int64     `json:"totalVnd"`
	LastRefCode string    `json:"lastRefCode"`
	LastIntent  int64     `json:"lastIntent"`
}

type CloudEvent struct {
	ID        int64     `json:"id"`
	Timestamp time.Time `json:"timestamp"`
	DeviceID  string    `json:"deviceId"`
	Type      string    `json:"type"` // connect, disconnect, heartbeat, payment, alert, command, paid, qr
	Message   string    `json:"message"`
	Data      any       `json:"data,omitempty"`
}

var (
	statsMu       sync.RWMutex
	deviceStats   = map[string]*DeviceStats{}
	eventHistory  []CloudEvent
	eventSeq      int64
	sseClientsMu  sync.Mutex
	sseClients    = map[chan CloudEvent]struct{}{}
	lastSentFrame map[string]any
)

func recordEvent(deviceID, typ, message string, data any) {
	statsMu.Lock()
	eventSeq++
	evt := CloudEvent{
		ID:        eventSeq,
		Timestamp: time.Now(),
		DeviceID:  deviceID,
		Type:      typ,
		Message:   message,
		Data:      data,
	}
	eventHistory = append(eventHistory, evt)
	if len(eventHistory) > 150 {
		eventHistory = eventHistory[len(eventHistory)-150:]
	}
	statsMu.Unlock()

	// Phát SSE tới các trình duyệt đang mở
	sseClientsMu.Lock()
	defer sseClientsMu.Unlock()
	for ch := range sseClients {
		select {
		case ch <- evt:
		default:
		}
	}
}

func getOrCreateStats(deviceID string) *DeviceStats {
	st, ok := deviceStats[deviceID]
	if !ok {
		st = &DeviceStats{
			ID:          deviceID,
			ConnectedAt: time.Now(),
			LastSeen:    time.Now(),
		}
		deviceStats[deviceID] = st
	}
	return st
}

// ── Tracking Hooks (được gọi từ WebSocket) ──────────────────────────────────

func trackConnect(deviceID string) {
	statsMu.Lock()
	st := getOrCreateStats(deviceID)
	st.Online = true
	st.ConnectedAt = time.Now()
	st.LastSeen = time.Now()
	statsMu.Unlock()
	recordEvent(deviceID, "connect", fmt.Sprintf("Thiết bị %s đã kết nối", deviceID), nil)
}

func trackDisconnect(deviceID string) {
	statsMu.Lock()
	if st, ok := deviceStats[deviceID]; ok {
		st.Online = false
		st.LastSeen = time.Now()
	}
	statsMu.Unlock()
	recordEvent(deviceID, "disconnect", fmt.Sprintf("Thiết bị %s ngắt kết nối", deviceID), nil)
}

func trackHeartbeat(deviceID, fw string, rssi, queueDepth int) {
	statsMu.Lock()
	st := getOrCreateStats(deviceID)
	st.Online = true
	st.LastSeen = time.Now()
	if fw != "" {
		st.FWVersion = fw
	}
	st.RSSI = rssi
	st.QueueDepth = queueDepth
	statsMu.Unlock()
	recordEvent(deviceID, "heartbeat", fmt.Sprintf("Heartbeat fw=%s rssi=%d tồn=%d", fw, rssi, queueDepth), map[string]any{
		"fw": fw, "rssi": rssi, "queue": queueDepth,
	})
}

func trackPayment(deviceID, method string, coins int, amount int64) {
	statsMu.Lock()
	st := getOrCreateStats(deviceID)
	st.LastSeen = time.Now()
	st.TotalCoins += coins
	st.TotalVND += amount
	statsMu.Unlock()
	recordEvent(deviceID, "payment", fmt.Sprintf("Tiền vào (%s): %d xu = %d đ", method, coins, amount), map[string]any{
		"method": method, "coins": coins, "amount": amount,
	})
}

func trackAlert(deviceID, code, severity, msg string, active bool) {
	state := "BẬT"
	if !active {
		state = "TẮT"
	}
	recordEvent(deviceID, "alert", fmt.Sprintf("Cảnh báo %s [%s] %s: %s", state, severity, code, msg), map[string]any{
		"code": code, "severity": severity, "message": msg, "active": active,
	})
}

func trackQR(deviceID, refCode string, intentID, amount int64) {
	statsMu.Lock()
	st := getOrCreateStats(deviceID)
	st.LastRefCode = refCode
	st.LastIntent = intentID
	statsMu.Unlock()
	recordEvent(deviceID, "qr", fmt.Sprintf("Phát QR động #%d: %s (%d đ)", intentID, refCode, amount), map[string]any{
		"intentId": intentID, "refCode": refCode, "amount": amount,
	})
}

func trackPaymentPaid(deviceID, refCode string, intentID, amount int64) {
	recordEvent(deviceID, "paid", fmt.Sprintf("Webhook xác nhận ĐÃ TRẢ: #%d %s (%d đ)", intentID, refCode, amount), map[string]any{
		"intentId": intentID, "refCode": refCode, "amount": amount,
	})
}

func trackCommandAck(deviceID string, cmdID int64, status, msg string) {
	recordEvent(deviceID, "command_ack", fmt.Sprintf("Máy ack lệnh #%d: status=%s (%s)", cmdID, status, msg), map[string]any{
		"commandId": cmdID, "status": status, "message": msg,
	})
}

// ── REST API & Webhooks ─────────────────────────────────────────────────────

func registerDashboard(mux *http.ServeMux) {
	mux.HandleFunc("/", handleDashboard)
	mux.HandleFunc("/dashboard", handleDashboard)
	mux.HandleFunc("/provision/", handleProvisionServe)
	mux.HandleFunc("/api/devices", handleAPIDevices)
	mux.HandleFunc("/api/devices/command", handleAPIDeviceCommand)
	mux.HandleFunc("/api/devices/dup", handleAPIDeviceDup)
	mux.HandleFunc("/api/devices/simulate-paid", handleAPISimulatePaid)
	mux.HandleFunc("/api/events", handleAPIEvents)
	mux.HandleFunc("/api/events/stream", handleSSEStream)
	mux.HandleFunc("/api/webhook/sepay", handleSepayWebhook)
	mux.HandleFunc("/api/webhook/payos", handlePayOSWebhook)
	mux.HandleFunc("/api/webhook/pay2s", handlePay2SWebhook)
	mux.HandleFunc("/api/webhook/tingee", handleTingeeWebhook)
}

func handleAPIDevices(w http.ResponseWriter, r *http.Request) {
	statsMu.RLock()
	defer statsMu.RUnlock()
	list := make([]DeviceStats, 0, len(deviceStats))
	for _, s := range deviceStats {
		list = append(list, *s)
	}
	writeJSON(w, map[string]any{"status": "ok", "devices": list})
}

func handleAPIEvents(w http.ResponseWriter, r *http.Request) {
	statsMu.RLock()
	defer statsMu.RUnlock()
	writeJSON(w, map[string]any{"status": "ok", "events": eventHistory})
}

func handleSSEStream(w http.ResponseWriter, r *http.Request) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "SSE không hỗ trợ", http.StatusBadRequest)
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")

	ch := make(chan CloudEvent, 20)
	sseClientsMu.Lock()
	sseClients[ch] = struct{}{}
	sseClientsMu.Unlock()

	defer func() {
		sseClientsMu.Lock()
		delete(sseClients, ch)
		sseClientsMu.Unlock()
	}()

	// Gửi ping chào đầu tiên
	fmt.Fprintf(w, "event: init\ndata: {\"status\":\"connected\"}\n\n")
	flusher.Flush()

	notify := r.Context().Done()
	for {
		select {
		case <-notify:
			return
		case evt := <-ch:
			b, _ := json.Marshal(evt)
			fmt.Fprintf(w, "data: %s\n\n", b)
			flusher.Flush()
		}
	}
}

type commandReq struct {
	DeviceID string         `json:"deviceId"`
	Action   string         `json:"action"`
	Params   map[string]any `json:"params"`
}

func handleAPIDeviceCommand(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "chỉ hỗ trợ POST", http.StatusMethodNotAllowed)
		return
	}
	var req commandReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "JSON lỗi", http.StatusBadRequest)
		return
	}
	if req.Action == "" {
		http.Error(w, "thiếu action", http.StatusBadRequest)
		return
	}
	d := pickDevice(req.DeviceID)
	if d == nil {
		http.Error(w, "thiết bị không online", http.StatusNotFound)
		return
	}
	if req.Params == nil {
		req.Params = map[string]any{}
	}

	cmdID := nextCommandID()
	frame := map[string]any{
		"type":      "command",
		"commandId": cmdID,
		"action":    req.Action,
		"params":    req.Params,
	}
	lastSentFrame = frame
	d.send(frame)
	recordEvent(d.id, "command", fmt.Sprintf("Gửi lệnh #%d: %s", cmdID, req.Action), req.Params)

	writeJSON(w, map[string]any{
		"status":    "ok",
		"commandId": cmdID,
		"deviceId":  d.id,
		"action":    req.Action,
	})
}

func handleAPIDeviceDup(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "chỉ hỗ trợ POST", http.StatusMethodNotAllowed)
		return
	}
	if lastSentFrame == nil {
		http.Error(w, "chưa có lệnh nào trước đó để gửi lại", http.StatusBadRequest)
		return
	}
	d := pickDevice("")
	if d == nil {
		http.Error(w, "thiết bị không online", http.StatusNotFound)
		return
	}
	d.send(lastSentFrame)
	cmdID, _ := lastSentFrame["commandId"].(int64)
	action, _ := lastSentFrame["action"].(string)
	recordEvent(d.id, "command", fmt.Sprintf("GỬI LẠI (dup) lệnh #%d: %s — máy phải trả duplicate", cmdID, action), lastSentFrame["params"])

	writeJSON(w, map[string]any{
		"status":    "ok",
		"message":   "đã gửi lại cùng commandId",
		"commandId": cmdID,
	})
}

type simulatePaidReq struct {
	DeviceID string `json:"deviceId"`
	IntentID int64  `json:"intentId"`
	Amount   int64  `json:"amount"`
	RefCode  string `json:"refCode"`
}

func handleAPISimulatePaid(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "chỉ hỗ trợ POST", http.StatusMethodNotAllowed)
		return
	}
	var req simulatePaidReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "JSON lỗi", http.StatusBadRequest)
		return
	}
	d := pickDevice(req.DeviceID)
	if d == nil {
		http.Error(w, "thiết bị không online", http.StatusNotFound)
		return
	}
	if req.IntentID == 0 {
		statsMu.RLock()
		if st, ok := deviceStats[d.id]; ok && st.LastIntent != 0 {
			req.IntentID = st.LastIntent
			req.RefCode = st.LastRefCode
		} else {
			req.IntentID = nextIntentID()
			req.RefCode = fmt.Sprintf("GTMOCKD%05d", req.IntentID)
		}
		statsMu.RUnlock()
	}
	if req.Amount == 0 {
		req.Amount = 20000
	}

	d.send(map[string]any{
		"type":     "payment_paid",
		"intentId": req.IntentID,
		"amount":   req.Amount,
		"refCode":  req.RefCode,
	})
	trackPaymentPaid(d.id, req.RefCode, req.IntentID, req.Amount)

	writeJSON(w, map[string]any{
		"status":   "ok",
		"intentId": req.IntentID,
		"amount":   req.Amount,
		"refCode":  req.RefCode,
	})
}

// ── Webhooks (SePAY & PayOS) ────────────────────────────────────────────────

// handleSepayWebhook nhận webhook từ cổng SePAY (https://sepay.vn).
// Payload SePAY gửi khi có tiền vào tài khoản:
//
//	{
//	  "id": 12345, "gateway": "Vietcombank", "transactionDate": "...",
//	  "accountNumber": "...", "content": "Thanh toan GTMOCKD00001",
//	  "transferType": "in", "transferAmount": 20000, "referenceCode": "MBVCB.123"
//	}
func handleSepayWebhook(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "lỗi đọc body", http.StatusBadRequest)
		return
	}
	var data struct {
		ID             int64   `json:"id"`
		Gateway        string  `json:"gateway"`
		Content        string  `json:"content"`
		TransferType   string  `json:"transferType"`
		TransferAmount float64 `json:"transferAmount"`
		ReferenceCode  string  `json:"referenceCode"`
	}
	if err := json.Unmarshal(body, &data); err != nil {
		http.Error(w, "JSON SePAY hỏng", http.StatusBadRequest)
		return
	}
	log.Printf("🏦 Webhook SePAY: [%s] %s +%.0f đ (%s)", data.Gateway, data.ReferenceCode, data.TransferAmount, data.Content)

	amount := int64(data.TransferAmount)
	refCode := extractRefCode(data.Content)
	target := findDeviceByRefOrAny(refCode)

	if target == nil {
		log.Printf("  ⚠ SePAY webhook: không tìm thấy thiết bị online nào để kích hoạt")
		writeJSON(w, map[string]any{"success": false, "message": "no device connected"})
		return
	}

	intentID := parseIntentID(refCode)
	target.send(map[string]any{
		"type":     "payment_paid",
		"intentId": intentID,
		"amount":   amount,
		"refCode":  refCode,
	})
	trackPaymentPaid(target.id, refCode, intentID, amount)

	writeJSON(w, map[string]any{"success": true, "deviceId": target.id, "amount": amount, "refCode": refCode})
}

// handlePayOSWebhook nhận webhook từ PayOS (https://payos.vn).
func handlePayOSWebhook(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "lỗi đọc body", http.StatusBadRequest)
		return
	}
	var data struct {
		Code string `json:"code"`
		Desc string `json:"desc"`
		Data struct {
			OrderCode   int64  `json:"orderCode"`
			Amount      int64  `json:"amount"`
			Description string `json:"description"`
			Reference   string `json:"reference"`
		} `json:"data"`
	}
	if err := json.Unmarshal(body, &data); err != nil {
		http.Error(w, "JSON PayOS hỏng", http.StatusBadRequest)
		return
	}
	log.Printf("🏦 Webhook PayOS: orderCode=%d amount=%d đ desc=%q", data.Data.OrderCode, data.Data.Amount, data.Data.Description)

	refCode := extractRefCode(data.Data.Description)
	target := findDeviceByRefOrAny(refCode)
	if target == nil {
		writeJSON(w, map[string]any{"success": false, "message": "no device online"})
		return
	}

	intentID := data.Data.OrderCode
	if intentID == 0 {
		intentID = parseIntentID(refCode)
	}

	target.send(map[string]any{
		"type":     "payment_paid",
		"intentId": intentID,
		"amount":   data.Data.Amount,
		"refCode":  refCode,
	})
	trackPaymentPaid(target.id, refCode, intentID, data.Data.Amount)

	writeJSON(w, map[string]any{"success": true, "deviceId": target.id, "amount": data.Data.Amount, "refCode": refCode})
}

// handlePay2SWebhook nhận webhook từ cổng Pay2S (https://pay2s.vn).
// Hỗ trợ cả 2 dạng:
// 1) Biến động số dư: {"transactions": [{"content": "...", "transferAmount": 20000, ...}]}
// 2) IPN đơn hàng: {"orderInfo": "...", "amount": 20000, "status": "SUCCESS"}
func handlePay2SWebhook(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "lỗi đọc body", http.StatusBadRequest)
		return
	}
	var payload struct {
		Transactions []struct {
			ID             int64   `json:"id"`
			Gateway        string  `json:"gateway"`
			Content        string  `json:"content"`
			TransferAmount float64 `json:"transferAmount"`
			TransferType   string  `json:"transferType"`
		} `json:"transactions"`
		ID             int64   `json:"id"`
		Gateway        string  `json:"gateway"`
		Content        string  `json:"content"`
		TransferAmount float64 `json:"transferAmount"`
		Amount         float64 `json:"amount"`
		OrderInfo      string  `json:"orderInfo"`
		Status         string  `json:"status"`
	}
	if err := json.Unmarshal(body, &payload); err != nil {
		http.Error(w, "JSON Pay2S hỏng", http.StatusBadRequest)
		return
	}

	type txItem struct {
		content string
		amount  int64
	}
	var items []txItem

	if len(payload.Transactions) > 0 {
		for _, tx := range payload.Transactions {
			items = append(items, txItem{content: tx.Content, amount: int64(tx.TransferAmount)})
		}
	} else {
		content := payload.Content
		if content == "" {
			content = payload.OrderInfo
		}
		amt := payload.TransferAmount
		if amt == 0 {
			amt = payload.Amount
		}
		items = append(items, txItem{content: content, amount: int64(amt)})
	}

	processedCount := 0
	for _, item := range items {
		log.Printf("🏦 Webhook Pay2S: content=%q amount=%d đ", item.content, item.amount)
		refCode := extractRefCode(item.content)
		target := findDeviceByRefOrAny(refCode)
		if target == nil {
			continue
		}
		intentID := parseIntentID(refCode)
		target.send(map[string]any{
			"type":     "payment_paid",
			"intentId": intentID,
			"amount":   item.amount,
			"refCode":  refCode,
		})
		trackPaymentPaid(target.id, refCode, intentID, item.amount)
		processedCount++
	}

	if processedCount == 0 && pickDevice("") == nil {
		writeJSON(w, map[string]any{"success": false, "message": "no device connected"})
		return
	}

	writeJSON(w, map[string]any{"success": true, "processed": processedCount})
}

// handleTingeeWebhook nhận webhook từ cổng Tingee (https://developers.tingee.vn).
// Payload:
//
//	{
//	  "orderId": "...", "amount": 20000, "paidAmount": 20000,
//	  "description": "Thanh toan GTMOCKD00001", "orderInfo": "...",
//	  "status": "success", "statusCode": "00"
//	}
func handleTingeeWebhook(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "lỗi đọc body", http.StatusBadRequest)
		return
	}
	var data struct {
		RequestId   string  `json:"requestId"`
		OrderId     string  `json:"orderId"`
		Amount      float64 `json:"amount"`
		PaidAmount  float64 `json:"paidAmount"`
		Description string  `json:"description"`
		OrderInfo   string  `json:"orderInfo"`
		Status      string  `json:"status"`
		StatusCode  string  `json:"statusCode"`
		BillId      string  `json:"billId"`
	}
	if err := json.Unmarshal(body, &data); err != nil {
		http.Error(w, "JSON Tingee hỏng", http.StatusBadRequest)
		return
	}

	isSuccess := data.Status == "success" || data.StatusCode == "00" || data.StatusCode == "02" || (data.Status == "" && data.StatusCode == "")
	if !isSuccess {
		log.Printf("  ⚠ Webhook Tingee báo trạng thái không thành công: status=%q, code=%q", data.Status, data.StatusCode)
		writeJSON(w, map[string]any{"code": "01", "message": "transaction not successful", "success": false})
		return
	}

	amount := int64(data.PaidAmount)
	if amount == 0 {
		amount = int64(data.Amount)
	}

	content := data.Description
	if content == "" {
		content = data.OrderInfo
	}
	if content == "" {
		content = data.OrderId
	}

	log.Printf("🏦 Webhook Tingee: orderId=%s billId=%s amount=%d đ content=%q", data.OrderId, data.BillId, amount, content)

	refCode := extractRefCode(content)
	target := findDeviceByRefOrAny(refCode)
	if target == nil {
		writeJSON(w, map[string]any{"code": "01", "success": false, "message": "no device connected"})
		return
	}

	intentID := parseIntentID(refCode)
	target.send(map[string]any{
		"type":     "payment_paid",
		"intentId": intentID,
		"amount":   amount,
		"refCode":  refCode,
	})
	trackPaymentPaid(target.id, refCode, intentID, amount)

	writeJSON(w, map[string]any{
		"code":     "00",
		"message":  "success",
		"success":  true,
		"deviceId": target.id,
		"amount":   amount,
		"refCode":  refCode,
	})
}

var refRegex = regexp.MustCompile(`(GTMOCK[A-Z0-9]+|INNO[A-Z0-9]+)`)

func extractRefCode(content string) string {
	matches := refRegex.FindStringSubmatch(content)
	if len(matches) > 1 {
		return matches[1]
	}
	return "GTMOCKD00001"
}

func parseIntentID(refCode string) int64 {
	var numStr string
	for _, c := range refCode {
		if c >= '0' && c <= '9' {
			numStr += string(c)
		}
	}
	if n, err := strconv.ParseInt(numStr, 10, 64); err == nil && n > 0 {
		return n
	}
	return nextIntentID()
}

func findDeviceByRefOrAny(refCode string) *device {
	statsMu.RLock()
	defer statsMu.RUnlock()
	for id, st := range deviceStats {
		if st.LastRefCode == refCode && st.Online {
			if d := pickDevice(id); d != nil {
				return d
			}
		}
	}
	return pickDevice("")
}

// ── Phục vụ giao diện tĩnh Web Provisioning ──────────────────────────────────

func handleProvisionServe(w http.ResponseWriter, r *http.Request) {
	// Tìm file tools/web-provision/index.html
	candidates := []string{
		"../web-provision/index.html",
		"tools/web-provision/index.html",
		"innoedge-sdk-esp32/tools/web-provision/index.html",
		"/Volumes/data/DEV2/Inno.EDGE/innoedge-sdk-esp32/tools/web-provision/index.html",
	}
	for _, path := range candidates {
		abs, err := filepath.Abs(path)
		if err == nil {
			if b, err := os.ReadFile(abs); err == nil {
				w.Header().Set("Content-Type", "text/html; charset=utf-8")
				w.Write(b)
				return
			}
		}
	}
	http.Error(w, "không tìm thấy web-provision/index.html", http.StatusNotFound)
}

// ── HTML Dashboard ──────────────────────────────────────────────────────────

func handleDashboard(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" && r.URL.Path != "/dashboard" {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write([]byte(dashboardHTML))
}

const dashboardHTML = `<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>InnoEdge Cloud Console (Mock)</title>
  <style>
    :root {
      --bg: #090d16;
      --panel: #111827;
      --border: #1f2937;
      --accent: #10b981;
      --accent-hover: #059669;
      --blue: #3b82f6;
      --text: #e5e7eb;
      --muted: #9ca3af;
      --heading: #f9fafb;
      --danger: #ef4444;
      --warning: #f59e0b;
      --mono: ui-monospace, SFMono-Regular, Menlo, monospace;
      --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: var(--sans);
      padding: 1.5rem;
      min-height: 100vh;
    }
    .layout {
      max-width: 1200px;
      margin: 0 auto;
      display: flex;
      flex-direction: column;
      gap: 1.5rem;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid var(--border);
      padding-bottom: 1rem;
      flex-wrap: wrap;
      gap: 1rem;
    }
    .brand {
      display: flex;
      align-items: center;
      gap: 0.75rem;
    }
    .brand-logo {
      width: 32px;
      height: 32px;
      background: linear-gradient(135deg, #10b981, #3b82f6);
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: 800;
      color: #fff;
    }
    .brand h1 { font-size: 1.35rem; color: var(--heading); }
    .brand-badge {
      font-size: 0.72rem;
      padding: 0.2rem 0.5rem;
      border-radius: 4px;
      background: rgba(16, 185, 129, 0.15);
      color: var(--accent);
      border: 1px solid rgba(16, 185, 129, 0.3);
      font-weight: 600;
      text-transform: uppercase;
    }
    .nav-links { display: flex; gap: 0.75rem; }
    .nav-btn {
      padding: 0.5rem 0.85rem;
      border-radius: 6px;
      background: var(--panel);
      border: 1px solid var(--border);
      color: var(--heading);
      font-size: 0.85rem;
      font-weight: 500;
      text-decoration: none;
      display: inline-flex;
      align-items: center;
      gap: 0.4rem;
      transition: border-color 0.15s;
    }
    .nav-btn:hover { border-color: var(--blue); }
    .stats-row {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 1rem;
    }
    .stat-card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 1.2rem;
    }
    .stat-label { font-size: 0.8rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.05em; }
    .stat-val { font-size: 1.6rem; font-weight: 700; color: var(--heading); margin-top: 0.25rem; font-family: var(--mono); }
    .grid-2 {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 1.5rem;
    }
    @media (max-width: 900px) { .grid-2 { grid-template-columns: 1fr; } }
    .card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 1.25rem;
      display: flex;
      flex-direction: column;
      gap: 1rem;
    }
    .card-title {
      font-size: 1.05rem;
      font-weight: 600;
      color: var(--heading);
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .device-list {
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
    }
    .device-card {
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 1rem;
      background: #0d1320;
    }
    .device-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 0.5rem;
    }
    .device-id { font-family: var(--mono); font-weight: 700; color: var(--blue); }
    .status-badge {
      font-size: 0.7rem;
      font-weight: 600;
      padding: 0.2rem 0.5rem;
      border-radius: 99px;
    }
    .status-online { background: rgba(16, 185, 129, 0.2); color: var(--accent); }
    .status-offline { background: rgba(239, 68, 68, 0.2); color: var(--danger); }
    .device-meta {
      font-size: 0.8rem;
      color: var(--muted);
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
      gap: 0.4rem;
      margin-bottom: 0.75rem;
      font-family: var(--mono);
    }
    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 0.5rem;
    }
    .btn {
      padding: 0.45rem 0.75rem;
      font-size: 0.8rem;
      font-weight: 600;
      border-radius: 6px;
      border: none;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 0.35rem;
      transition: opacity 0.15s, transform 0.05s;
    }
    .btn:active { transform: scale(0.98); }
    .btn-green { background: #059669; color: #fff; }
    .btn-blue { background: #2563eb; color: #fff; }
    .btn-amber { background: #d97706; color: #fff; }
    .btn-red { background: #dc2626; color: #fff; }
    .btn-purple { background: #7c3aed; color: #fff; }
    .btn-gray { background: #374151; color: var(--heading); }
    .log-container {
      height: 480px;
      background: #060910;
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 0.75rem;
      overflow-y: auto;
      font-family: var(--mono);
      font-size: 0.76rem;
      display: flex;
      flex-direction: column;
      gap: 0.3rem;
    }
    .log-line {
      display: flex;
      gap: 0.5rem;
      line-height: 1.4;
      word-break: break-all;
    }
    .log-time { color: var(--muted); flex-shrink: 0; }
    .log-tag {
      padding: 0 0.3rem;
      border-radius: 3px;
      font-weight: 600;
      font-size: 0.68rem;
      text-transform: uppercase;
      flex-shrink: 0;
    }
    .tag-connect { background: rgba(16, 185, 129, 0.2); color: #34d399; }
    .tag-payment { background: rgba(245, 158, 11, 0.2); color: #fbbf24; }
    .tag-command { background: rgba(59, 130, 246, 0.2); color: #60a5fa; }
    .tag-alert { background: rgba(239, 68, 68, 0.2); color: #f87171; }
    .tag-qr { background: rgba(168, 85, 247, 0.2); color: #c084fc; }
    .tag-heartbeat { background: rgba(75, 85, 99, 0.3); color: #9ca3af; }
    .webhook-box {
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 0.85rem;
      background: #0d1320;
    }
    .webhook-box pre {
      background: #060910;
      padding: 0.5rem;
      border-radius: 6px;
      font-size: 0.75rem;
      font-family: var(--mono);
      overflow-x: auto;
      margin-top: 0.5rem;
    }
  </style>
</head>
<body>
  <div class="layout">
    <header>
      <div class="brand">
        <div class="brand-logo">IE</div>
        <div>
          <h1>InnoEdge Cloud Console</h1>
          <span style="font-size: 0.8rem; color: var(--muted);">Trực quan hoá & điều khiển thiết bị IoT</span>
        </div>
        <span class="brand-badge">Mock Server</span>
      </div>
      <div class="nav-links">
        <a href="/provision/" target="_blank" class="nav-btn">
          <span>📡 BLE Provisioning</span>
        </a>
        <a href="https://github.com/nguyenduchoai/innoedge-platform" target="_blank" class="nav-btn">
          <span>📖 Tài liệu Protocol v1</span>
        </a>
      </div>
    </header>

    <div class="stats-row">
      <div class="stat-card">
        <div class="stat-label">Thiết bị Online</div>
        <div class="stat-val" id="statOnline">0</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">Tổng doanh thu nhận</div>
        <div class="stat-val" id="statRevenue">0 đ</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">Tổng số xu / vé</div>
        <div class="stat-val" id="statCoins">0</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">SSE Realtime</div>
        <div class="stat-val" id="statSSE" style="color: var(--accent);">Nối...</div>
      </div>
    </div>

    <div class="grid-2">
      <!-- Cột trái: Thiết bị & Điều khiển -->
      <div class="card">
        <div class="card-title">
          <span>Danh sách Thiết bị</span>
          <button class="btn btn-gray" onclick="loadDevices()">↻ Làm mới</button>
        </div>
        <div id="deviceList" class="device-list">
          <div style="color: var(--muted); font-size: 0.85rem; padding: 1rem; text-align: center;">
            Đang tìm thiết bị nối vào... (hãy flash firmware và mở cổng)
          </div>
        </div>

        <div class="card-title" style="margin-top: 1rem;">
          <span>Giả lập Webhook Ngân Hàng</span>
        </div>
        <div class="webhook-box">
          <div style="font-size: 0.85rem; color: var(--heading); font-weight: 600;">
            Thử nghiệm Webhook Cổng Thanh Toán (SePAY / PayOS / Pay2S / Tingee)
          </div>
          <div style="font-size: 0.78rem; color: var(--muted); margin-top: 0.25rem;">
            Bấm nút dưới để giả lập webhook ngân hàng bắn tiền về thiết bị đang online:
          </div>
          <div style="margin-top: 0.75rem; display: flex; gap: 0.5rem; flex-wrap: wrap;">
            <button class="btn btn-green" onclick="testSepayWebhook(20000)">💰 Test SePAY (20k)</button>
            <button class="btn btn-blue" onclick="testPayosWebhook(50000)">🏦 Test PayOS (50k)</button>
            <button class="btn btn-amber" onclick="testPay2sWebhook(20000)">⚡ Test Pay2S (20k)</button>
            <button class="btn btn-purple" onclick="testTingeeWebhook(30000)">🔔 Test Tingee (30k)</button>
          </div>
        </div>
      </div>

      <!-- Cột phải: Live Log -->
      <div class="card">
        <div class="card-title">
          <span>Nhật ký Sự kiện Realtime (Live Stream)</span>
          <button class="btn btn-gray" onclick="clearLogs()">Xoá log</button>
        </div>
        <div class="log-container" id="logBox"></div>
      </div>
    </div>
  </div>

  <script>
    let devices = [];
    const logBox = document.getElementById('logBox');

    function appendLog(evt) {
      const line = document.createElement('div');
      line.className = 'log-line';

      const d = evt.timestamp ? new Date(evt.timestamp) : new Date();
      const timeStr = d.toLocaleTimeString('vi-VN');

      let tagClass = 'tag-heartbeat';
      if (evt.type === 'connect' || evt.type === 'paid') tagClass = 'tag-connect';
      else if (evt.type === 'payment') tagClass = 'tag-payment';
      else if (evt.type === 'command' || evt.type === 'command_ack') tagClass = 'tag-command';
      else if (evt.type === 'alert') tagClass = 'tag-alert';
      else if (evt.type === 'qr') tagClass = 'tag-qr';

      line.innerHTML = '<span class="log-time">[' + timeStr + ']</span>' +
        '<span class="log-tag ' + tagClass + '">' + evt.type + '</span>' +
        '<span style="color: #93c5fd;">' + (evt.deviceId ? evt.deviceId + ': ' : '') + '</span>' +
        '<span>' + evt.message + '</span>';

      logBox.appendChild(line);
      logBox.scrollTop = logBox.scrollHeight;
    }

    function clearLogs() { logBox.innerHTML = ''; }

    async function loadDevices() {
      try {
        const res = await fetch('/api/devices');
        const json = await res.json();
        devices = json.devices || [];
        renderDevices();
      } catch (e) {
        console.error('load devices error', e);
      }
    }

    function renderDevices() {
      const container = document.getElementById('deviceList');
      if (devices.length === 0) {
        container.innerHTML = '<div style="color: var(--muted); font-size: 0.85rem; padding: 1.5rem; text-align: center;">Chưa có thiết bị nào kết nối.<br>Hãy cấu hình IP LAN trong <code>idf.py menuconfig</code> và khởi động bo ESP32!</div>';
        document.getElementById('statOnline').textContent = '0';
        document.getElementById('statRevenue').textContent = '0 đ';
        document.getElementById('statCoins').textContent = '0';
        return;
      }

      let onlineCount = 0;
      let totalVnd = 0;
      let totalCoins = 0;

      container.innerHTML = '';
      devices.forEach(d => {
        if (d.online) onlineCount++;
        totalVnd += (d.totalVnd || 0);
        totalCoins += (d.totalCoins || 0);

        const card = document.createElement('div');
        card.className = 'device-card';
        card.innerHTML = 
          '<div class="device-header">' +
            '<div><span class="device-id">' + d.id + '</span></div>' +
            '<span class="status-badge ' + (d.online ? 'status-online' : 'status-offline') + '">' + (d.online ? 'ONLINE' : 'OFFLINE') + '</span>' +
          '</div>' +
          '<div class="device-meta">' +
            '<div>FW: <strong>' + (d.fwVersion || '0.1.0') + '</strong></div>' +
            '<div>RSSI: <strong>' + (d.rssi || 0) + ' dBm</strong></div>' +
            '<div>Tồn NVS: <strong>' + (d.queueDepth || 0) + '</strong></div>' +
            '<div>Thu: <strong>' + (d.totalVnd || 0).toLocaleString() + ' đ</strong></div>' +
          '</div>' +
          '<div class="actions">' +
            '<button class="btn btn-green" onclick="sendCommand(\'' + d.id + '\', \'dispense\', {amountVnd: 20000})">🪙 Nhả 20k</button>' +
            '<button class="btn btn-blue" onclick="sendCommand(\'' + d.id + '\', \'led\', {on: true})">💡 Bật LED</button>' +
            '<button class="btn btn-gray" onclick="sendCommand(\'' + d.id + '\', \'led\', {on: false})">Tắt LED</button>' +
            '<button class="btn btn-amber" onclick="simulatePaid(\'' + d.id + '\', 20000)">💳 Báo Đã Trả</button>' +
            '<button class="btn btn-blue" onclick="sendDup()">🔁 Gửi Lại (Dup)</button>' +
            '<button class="btn btn-red" onclick="sendCommand(\'' + d.id + '\', \'reboot\', {})">🔄 Reboot</button>' +
          '</div>';
        container.appendChild(card);
      });

      document.getElementById('statOnline').textContent = onlineCount;
      document.getElementById('statRevenue').textContent = totalVnd.toLocaleString() + ' đ';
      document.getElementById('statCoins').textContent = totalCoins;
    }

    async function sendCommand(deviceId, action, params) {
      try {
        const res = await fetch('/api/devices/command', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ deviceId, action, params })
        });
        const json = await res.json();
        if (json.status !== 'ok') alert('Lỗi: ' + json.message);
      } catch (e) {
        alert('Lỗi gửi lệnh: ' + e.message);
      }
    }

    async function sendDup() {
      try {
        const res = await fetch('/api/devices/dup', { method: 'POST' });
        const json = await res.json();
        if (json.status !== 'ok') alert('Lỗi: ' + json.message);
      } catch (e) {
        alert('Lỗi: ' + e.message);
      }
    }

    async function simulatePaid(deviceId, amount) {
      try {
        await fetch('/api/devices/simulate-paid', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ deviceId, amount })
        });
      } catch (e) {
        alert('Lỗi: ' + e.message);
      }
    }

    async function testSepayWebhook(amount) {
      try {
        const payload = {
          id: Math.floor(Math.random() * 100000),
          gateway: "Vietcombank",
          transactionDate: new Date().toISOString(),
          accountNumber: "0123456789",
          content: "Thanh toan GTMOCKD00001",
          transferType: "in",
          transferAmount: amount,
          referenceCode: "MBVCB." + Math.floor(Math.random() * 90000000 + 10000000)
        };
        const res = await fetch('/api/webhook/sepay', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const json = await res.json();
        if (json.success) {
          appendLog({ type: 'paid', message: 'Đã bắn webhook SePAY thành công (' + amount + ' đ)' });
        } else {
          alert('Không bắn được webhook: ' + json.message);
        }
      } catch (e) {
        alert('Lỗi webhook: ' + e.message);
      }
    }

    async function testPayosWebhook(amount) {
      try {
        const payload = {
          code: "00",
          desc: "success",
          data: {
            orderCode: Math.floor(Math.random() * 90000 + 10000),
            amount: amount,
            description: "Thanh toan don GTMOCKD00001",
            reference: "PAYOS" + Math.floor(Math.random() * 900000)
          }
        };
        const res = await fetch('/api/webhook/payos', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const json = await res.json();
        if (json.success) {
          appendLog({ type: 'paid', message: 'Đã bắn webhook PayOS thành công (' + amount + ' đ)' });
        } else {
          alert('Không bắn được webhook: ' + json.message);
        }
      } catch (e) {
        alert('Lỗi webhook: ' + e.message);
      }
    }

    async function testPay2sWebhook(amount) {
      try {
        const payload = {
          transactions: [
            {
              id: Math.floor(Math.random() * 90000 + 10000),
              gateway: "ACB",
              transactionDate: new Date().toISOString(),
              content: "Thanh toan GTMOCKD00001",
              transferAmount: amount,
              transferType: "IN"
            }
          ]
        };
        const res = await fetch('/api/webhook/pay2s', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const json = await res.json();
        if (json.success) {
          appendLog({ type: 'paid', message: 'Đã bắn webhook Pay2S thành công (' + amount + ' đ)' });
        } else {
          alert('Không bắn được webhook: ' + json.message);
        }
      } catch (e) {
        alert('Lỗi webhook: ' + e.message);
      }
    }

    async function testTingeeWebhook(amount) {
      try {
        const payload = {
          requestId: "test-" + Math.floor(Math.random() * 900000),
          orderId: "SDK-" + Math.floor(Math.random() * 900000),
          amount: amount,
          paidAmount: amount,
          description: "Thanh toan GTMOCKD00001",
          orderInfo: "GTMOCKD00001",
          status: "success",
          statusCode: "00",
          billId: "TIN" + Math.floor(Math.random() * 900000),
          paymentMethod: "qr"
        };
        const res = await fetch('/api/webhook/tingee', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const json = await res.json();
        if (json.success) {
          appendLog({ type: 'paid', message: 'Đã bắn webhook Tingee thành công (' + amount + ' đ)' });
        } else {
          alert('Không bắn được webhook: ' + json.message);
        }
      } catch (e) {
        alert('Lỗi webhook: ' + e.message);
      }
    }

    // Kết nối SSE Realtime Stream
    function initSSE() {
      const sseStatus = document.getElementById('statSSE');
      const es = new EventSource('/api/events/stream');

      es.onopen = () => {
        sseStatus.textContent = 'ONLINE';
        sseStatus.style.color = 'var(--accent)';
      };

      es.onmessage = (e) => {
        try {
          const evt = JSON.parse(e.data);
          appendLog(evt);
          // Cập nhật lại device list khi có event connect/disconnect/payment/heartbeat
          if (['connect', 'disconnect', 'heartbeat', 'payment'].includes(evt.type)) {
            loadDevices();
          }
        } catch (err) {}
      };

      es.onerror = () => {
        sseStatus.textContent = 'Mất kết nối';
        sseStatus.style.color = 'var(--danger)';
      };
    }

    // Khởi động
    loadDevices();
    initSSE();
  </script>
</body>
</html>
`
