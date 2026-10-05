// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// innoedge-agent: Daemon tiến trình nền Linux cho Raspberry Pi, Banana Pi & Linux SBC.
// Cung cấp REST API & SSE Server-Sent Events tại localhost:8089 để mọi ứng dụng giao diện
// (Electron, Flutter, Qt, Python, Web Browser) có thể tích hợp thanh toán và IoT chỉ bằng curl.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"
)

type Config struct {
	DeviceID   string `json:"device_id"`
	CloudURL   string `json:"cloud_url"`
	FWVersion  string `json:"fw_version"`
	ListenAddr string `json:"listen_addr"`
}

type AgentState struct {
	mu         sync.Mutex
	DeviceID   string    `json:"device_id"`
	Connected  bool      `json:"connected"`
	Watermark  int64     `json:"watermark"`
	TotalPaid  int64     `json:"total_paid_vnd"`
	StartTime  time.Time `json:"start_time"`
	clients    map[chan string]bool
	ledgerPath string
}

func newAgentState(deviceID, storageDir string) *AgentState {
	s := &AgentState{
		DeviceID:   deviceID,
		Connected:  true,
		StartTime:  time.Now(),
		clients:    make(map[chan string]bool),
		ledgerPath: filepath.Join(storageDir, "agent_ledger.json"),
	}
	s.loadLedger()
	return s
}

func (s *AgentState) loadLedger() {
	s.mu.Lock()
	defer s.mu.Unlock()
	b, err := os.ReadFile(s.ledgerPath)
	if err == nil {
		var data struct {
			Watermark int64 `json:"watermark"`
			TotalPaid int64 `json:"total_paid"`
		}
		if json.Unmarshal(b, &data) == nil {
			s.Watermark = data.Watermark
			s.TotalPaid = data.TotalPaid
		}
	}
}

func (s *AgentState) saveLedger() {
	data := map[string]any{
		"device_id":  s.DeviceID,
		"watermark":  s.Watermark,
		"total_paid": s.TotalPaid,
		"updated_at": time.Now().Unix(),
	}
	b, _ := json.MarshalIndent(data, "", "  ")
	_ = os.WriteFile(s.ledgerPath, b, 0644)
}

func (s *AgentState) broadcastEvent(event string, payload any) {
	s.mu.Lock()
	defer s.mu.Unlock()
	data, _ := json.Marshal(map[string]any{
		"event":     event,
		"payload":   payload,
		"timestamp": time.Now().Unix(),
	})
	msg := fmt.Sprintf("event: %s\ndata: %s\n\n", event, string(data))
	for ch := range s.clients {
		select {
		case ch <- msg:
		default:
		}
	}
}

func main() {
	cloudURL := flag.String("cloud", "ws://127.0.0.1:8080/ws", "InnoEdge Cloud WebSocket URL")
	port := flag.String("port", "8089", "Cổng lắng nghe REST API cục bộ trên Pi")
	devID := flag.String("device", "", "Mã định danh thiết bị (mặc định tự nhận diện)")
	flag.Parse()

	storageDir := filepath.Join(os.Getenv("HOME"), ".innoedge")
	_ = os.MkdirAll(storageDir, 0755)

	if *devID == "" {
		hostname, _ := os.Hostname()
		*devID = fmt.Sprintf("PI-%s", hostname)
	}

	state := newAgentState(*devID, storageDir)
	log.Printf("🚀 innoedge-agent khởi chạy trên Linux SBC (ID: %s) tại :%s", state.DeviceID, *port)
	log.Printf("   Kết nối Cloud: %s | Lưu trữ: %s", *cloudURL, storageDir)

	mux := http.NewServeMux()

	// 1. GET /api/status - Trạng thái máy
	mux.HandleFunc("/api/status", func(w http.ResponseWriter, r *http.Request) {
		state.mu.Lock()
		defer state.mu.Unlock()
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"device_id":      state.DeviceID,
			"connected":      state.Connected,
			"watermark":      state.Watermark,
			"total_paid_vnd": state.TotalPaid,
			"uptime_sec":     int(time.Since(state.StartTime).Seconds()),
		})
	})

	// 2. POST /api/pay/qr - Khởi tạo mã VietQR từ giao diện Kiosk
	mux.HandleFunc("/api/pay/qr", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var req struct {
			AmountVnd int64  `json:"amount_vnd"`
			ItemName  string `json:"item_name"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		refCode := fmt.Sprintf("REF%d", time.Now().Unix()%100000)
		log.Printf("[Agent] Sinh mã QR thanh toán: %d đ (Món: %s, Ref: %s)", req.AmountVnd, req.ItemName, refCode)

		qrData := map[string]any{
			"payload":    fmt.Sprintf("00020101021238540010A00000072701240006970422...%d", req.AmountVnd),
			"amount_vnd": req.AmountVnd,
			"ref_code":   refCode,
			"expires_s":  300,
		}
		state.broadcastEvent("qr_ready", qrData)

		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(qrData)
	})

	// 3. POST /api/simulate/paid - Giả lập sự kiện ngân hàng báo tiền về (cho dev)
	mux.HandleFunc("/api/simulate/paid", func(w http.ResponseWriter, r *http.Request) {
		var req struct {
			AmountVnd int64 `json:"amount_vnd"`
			IntentID  int64 `json:"intent_id"`
		}
		_ = json.NewDecoder(r.Body).Decode(&req)
		if req.AmountVnd <= 0 {
			req.AmountVnd = 25000
		}
		if req.IntentID <= 0 {
			req.IntentID = time.Now().Unix()
		}

		state.mu.Lock()
		state.TotalPaid += req.AmountVnd
		state.saveLedger()
		state.mu.Unlock()

		log.Printf("✓ [Agent] Ngân hàng xác nhận đã nhận %d đ (intent=%d)", req.AmountVnd, req.IntentID)
		state.broadcastEvent("paid", req)

		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{"status": "ok", "paid": req})
	})

	// 4. GET /api/events - Server-Sent Events (SSE) đẩy luồng dữ liệu thời gian thực cho App Kiosk
	mux.HandleFunc("/api/events", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("Access-Control-Allow-Origin", "*")

		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "SSE not supported", http.StatusInternalServerError)
			return
		}

		ch := make(chan string, 16)
		state.mu.Lock()
		state.clients[ch] = true
		state.mu.Unlock()

		defer func() {
			state.mu.Lock()
			delete(state.clients, ch)
			close(ch)
			state.mu.Unlock()
		}()

		// Gửi ping chào mừng
		_, _ = fmt.Fprintf(w, "event: connected\ndata: {\"device\":\"%s\"}\n\n", state.DeviceID)
		flusher.Flush()

		ctx := r.Context()
		for {
			select {
			case <-ctx.Done():
				return
			case msg := <-ch:
				_, _ = fmt.Fprint(w, msg)
				flusher.Flush()
			}
		}
	})

	server := &http.Server{
		Addr:    ":" + *port,
		Handler: mux,
	}

	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("Lỗi lắng nghe: %v", err)
	}
}
