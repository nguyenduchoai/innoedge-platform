// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Test -voice KHÔNG cần API key: giả lập ASR/TTS (HTTP), LLM (HTTP) và thiết bị
// (WS). Kiểm cả vòng: listen start → PCM → stop → ASR → LLM → TTS → PCM về máy.
package main

import (
	"encoding/binary"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func TestWavWrapHeaderDung(t *testing.T) {
	pcm := make([]byte, 3200) // 100ms @16k
	w := wavWrap(pcm, 16000)
	if len(w) != 44+len(pcm) {
		t.Fatalf("WAV dài %d, mong %d", len(w), 44+len(pcm))
	}
	if string(w[0:4]) != "RIFF" || string(w[8:12]) != "WAVE" || string(w[36:40]) != "data" {
		t.Error("thiếu chunk RIFF/WAVE/data")
	}
	if binary.LittleEndian.Uint32(w[24:]) != 16000 {
		t.Error("sample rate sai")
	}
	if binary.LittleEndian.Uint16(w[22:]) != 1 || binary.LittleEndian.Uint16(w[34:]) != 16 {
		t.Error("phải mono 16-bit")
	}
	if binary.LittleEndian.Uint32(w[40:]) != uint32(len(pcm)) {
		t.Error("data size sai")
	}
}

func TestResample24to16(t *testing.T) {
	// 240 mẫu 24k = 10ms → 160 mẫu 16k
	src := make([]byte, 240*2)
	for i := 0; i < 240; i++ {
		binary.LittleEndian.PutUint16(src[i*2:], uint16(int16(i*100)))
	}
	out := resample24to16(src)
	if len(out) != 160*2 {
		t.Fatalf("ra %d byte, mong %d", len(out), 160*2)
	}
	// đơn điệu tăng (tín hiệu vào là dốc) và mẫu cuối gần mẫu cuối vào
	prev := int16(-1)
	for i := 0; i < 160; i++ {
		v := int16(binary.LittleEndian.Uint16(out[i*2:]))
		if v < prev {
			t.Fatalf("mẫu %d giảm: %d < %d", i, v, prev)
		}
		prev = v
	}
	if prev < 23000 {
		t.Errorf("mẫu cuối %d quá thấp — resample cắt đuôi", prev)
	}
	if resample24to16([]byte{1, 2}) != nil {
		t.Error("đầu vào < 2 mẫu phải trả nil")
	}
}

// Server giả cho cả ASR lẫn TTS, ghi lại nó nhận được gì.
func fakeSpeech(t *testing.T) (*httptest.Server, *string) {
	t.Helper()
	var gotTTS string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch {
		case strings.HasSuffix(r.URL.Path, "/audio/transcriptions"):
			if err := r.ParseMultipartForm(1 << 20); err != nil {
				http.Error(w, err.Error(), 400)
				return
			}
			f, _, err := r.FormFile("file")
			if err != nil {
				http.Error(w, "thiếu file", 400)
				return
			}
			head := make([]byte, 4)
			_, _ = io.ReadFull(f, head)
			if string(head) != "RIFF" {
				http.Error(w, "không phải WAV", 400)
				return
			}
			if r.FormValue("language") != "vi" {
				http.Error(w, "thiếu language=vi", 400)
				return
			}
			_, _ = w.Write([]byte(`{"text":"bật đèn"}`))
		case strings.HasSuffix(r.URL.Path, "/audio/speech"):
			var in struct {
				Input          string `json:"input"`
				ResponseFormat string `json:"response_format"`
			}
			_ = json.NewDecoder(r.Body).Decode(&in)
			if in.ResponseFormat != "pcm" {
				http.Error(w, "phải xin pcm", 400)
				return
			}
			gotTTS = in.Input
			_, _ = w.Write(make([]byte, 24000*2/10)) // 100ms @24k im lặng
		default:
			http.NotFound(w, r)
		}
	}))
	t.Cleanup(srv.Close)
	return srv, &gotTTS
}

func TestVoiceVongTronDayDu(t *testing.T) {
	oldV, oldA, oldURL, oldKey := *voiceMode, *aiMode, *speechURL, *speechKey
	*voiceMode, *aiMode, *speechKey = true, true, "test"
	defer func() { *voiceMode, *aiMode, *speechURL, *speechKey = oldV, oldA, oldURL, oldKey }()
	for len(eventCh) > 0 {
		<-eventCh
	}

	speech, gotTTS := fakeSpeech(t)
	*speechURL = speech.URL

	cloud := httptest.NewServer(newMux())
	defer cloud.Close()
	dev := dial(t, cloud)
	_ = dev.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, dev, "hello")
	waitDevices(t, 1)

	// Thiết bị: listen start → 500ms PCM (25 khung) → stop
	_ = dev.WriteJSON(map[string]any{"type": "listen", "state": "start", "format": "pcm16", "rate": 16000})
	for i := 0; i < 25; i++ {
		_ = dev.WriteMessage(websocket.BinaryMessage, make([]byte, pcmFrameB))
	}
	_ = dev.WriteJSON(map[string]any{"type": "listen", "state": "stop"})

	// 1. Máy nhận stt với text ASR trả về.
	stt := readUntil(t, dev, "stt")
	if stt["text"] != "bật đèn" {
		t.Errorf("stt.text = %v", stt["text"])
	}

	// 2. Câu nói vào hàng đợi AI như lượt user (không phải JSON sự kiện).
	select {
	case ev := <-eventCh:
		if ev.Name != "speech" {
			t.Errorf("mong sự kiện speech, nhận %s", ev.Name)
		}
		var said string
		_ = json.Unmarshal(ev.Data, &said)
		if said != "bật đèn" {
			t.Errorf("speech data = %q", said)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("câu nói không tới AI")
	}

	// 3. speak(): TTS → tts start → PCM đúng nhịp → tts stop
	d := pickDevice("")
	go speak(d, "Đã bật đèn.")
	readUntil(t, dev, "tts")
	frames, bytesTotal := 0, 0
	start := time.Now()
	for {
		_ = dev.SetReadDeadline(time.Now().Add(3 * time.Second))
		mt, data, err := dev.ReadMessage()
		if err != nil {
			t.Fatalf("đọc lỗi: %v", err)
		}
		if mt == websocket.BinaryMessage {
			frames++
			bytesTotal += len(data)
			if len(data) > pcmFrameB {
				t.Errorf("khung %d byte > %d — máy sẽ tràn buffer WS", len(data), pcmFrameB)
			}
			continue
		}
		var m map[string]any
		_ = json.Unmarshal(data, &m)
		if m["type"] == "tts" && m["state"] == "stop" {
			break
		}
	}
	if *gotTTS != "Đã bật đèn." {
		t.Errorf("TTS nhận %q", *gotTTS)
	}
	// 100ms @24k → ~100ms @16k = 3200 byte = 5 khung
	if bytesTotal < 3000 || bytesTotal > 3400 {
		t.Errorf("PCM về máy %d byte, mong ~3200 (đã resample 24k→16k)", bytesTotal)
	}
	// Đúng nhịp: 5 khung × 20ms ≥ 80ms, không được bắn tức thì
	if el := time.Since(start); el < 80*time.Millisecond {
		t.Errorf("gửi %d khung trong %s — không đúng nhịp thật, máy tràn đệm", frames, el)
	}
}

func TestVoiceClipQuaNganBiBo(t *testing.T) {
	oldV := *voiceMode
	*voiceMode = true
	defer func() { *voiceMode = oldV }()
	speech, gotTTS := fakeSpeech(t)
	old := *speechURL
	*speechURL = speech.URL
	defer func() { *speechURL = old }()

	processClip(&device{id: "X"}, make([]byte, pcmFrameB*3)) // 60ms — bấm nhầm
	time.Sleep(100 * time.Millisecond)
	if *gotTTS != "" {
		t.Error("clip 60ms mà vẫn gọi ASR/TTS")
	}
}

func TestVoiceTranClip(t *testing.T) {
	d := &device{id: "Y"}
	c := clipOf(d.id)
	clipsMu.Lock()
	c.buf.Reset()
	c.listening = true
	clipsMu.Unlock()
	big := make([]byte, maxClipByte)
	handleBinary(d, big)
	handleBinary(d, []byte{1, 2, 3, 4}) // vượt trần → bỏ
	clipsMu.Lock()
	n := c.buf.Len()
	c.listening = false
	clipsMu.Unlock()
	if n != maxClipByte {
		t.Errorf("clip %d byte, trần phải là %d", n, maxClipByte)
	}
}

// Lỗi review: listen start không huỷ speak() đang chạy → máy phát lại câu cũ đè
// lên lúc thu. Sửa: speak huỷ được; listen start cancel; vẫn gửi tts stop.
func TestBargeInHuySpeakDangChay(t *testing.T) {
	oldV, oldURL, oldKey := *voiceMode, *speechURL, *speechKey
	*voiceMode, *speechKey = true, "test"
	defer func() { *voiceMode, *speechURL, *speechKey = oldV, oldURL, oldKey }()

	// TTS giả trả 2 giây audio @24k → speak() sẽ bơm ~100 khung trong 2s
	long := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write(make([]byte, 24000*2*2))
	}))
	defer long.Close()
	*speechURL = long.URL

	cloud := httptest.NewServer(newMux())
	defer cloud.Close()
	dev := dial(t, cloud)
	_ = dev.WriteJSON(map[string]any{"type": "hello"})
	readUntil(t, dev, "hello")
	waitDevices(t, 1)
	d := pickDevice("")

	done := make(chan struct{})
	go func() { speak(d, "câu rất dài"); close(done) }()
	readUntil(t, dev, "tts") // start

	// Sau 200ms người dùng bấm nói → listen start
	time.Sleep(200 * time.Millisecond)
	_ = dev.WriteJSON(map[string]any{"type": "listen", "state": "start", "format": "pcm16", "rate": 16000})

	// speak phải kết thúc NGAY (không đợi hết 2s) và gửi tts stop
	select {
	case <-done:
	case <-time.After(700 * time.Millisecond):
		t.Fatal("speak() không bị huỷ sau listen start — cloud vẫn bơm PCM đè lên lúc thu")
	}
	sawStop := false
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) && !sawStop {
		_ = dev.SetReadDeadline(deadline)
		mt, data, err := dev.ReadMessage()
		if err != nil {
			break
		}
		if mt == websocket.BinaryMessage {
			continue
		}
		var m map[string]any
		_ = json.Unmarshal(data, &m)
		sawStop = m["type"] == "tts" && m["state"] == "stop"
	}
	if !sawStop {
		t.Error("bị huỷ nhưng không gửi tts stop — máy không khép được trạng thái phát")
	}
}
