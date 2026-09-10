// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Chế độ -voice: giọng nói hai chiều kiểu Xiaozhi, trên giao thức InnoEdge.
//
//	máy  ──listen start──▶            ┌─ ASR ─▶ text ─▶ Claude (tool calling) ─▶ text ─▶ TTS ─┐
//	máy  ──PCM 16k……────▶  gom clip ──┘                                                       │
//	máy  ──listen stop───▶                                                                   │
//	máy  ◀──stt{text}────  ◀──tts start── ◀──PCM 16k, 20ms/khung, đúng nhịp thật── ◀──tts stop─┘
//
// ASR/TTS gọi API dạng OpenAI (/audio/transcriptions, /audio/speech) bằng
// net/http thuần — không SDK, không cgo. Đổi -speech-url là chạy với server
// local (faster-whisper-server, LocalAI, openedai-speech…). LLM vẫn là Claude.
//
// Không Opus, không VAD, không wake word: push-to-talk là đủ để dạy và demo.
package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"mime/multipart"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"
)

var (
	voiceMode = flag.Bool("voice", false, "bật giọng nói: nhận PCM từ máy → ASR → AI → TTS → PCM về máy (cần -ai)")
	speechURL = flag.String("speech-url", "https://api.openai.com/v1",
		"base URL API giọng nói dạng OpenAI (/audio/transcriptions + /audio/speech)")
	speechKey = flag.String("speech-key", "", "API key giọng nói (mặc định: $OPENAI_API_KEY)")
	asrModel  = flag.String("asr-model", "whisper-1", "model ASR")
	ttsModel  = flag.String("tts-model", "tts-1", "model TTS")
	ttsVoice  = flag.String("tts-voice", "nova", "giọng TTS")
	asrLang   = flag.String("asr-lang", "vi", "ngôn ngữ ASR (ISO-639-1)")
)

const (
	pcmRate     = 16000
	pcmFrameMS  = 20
	pcmFrameB   = pcmRate * 2 * pcmFrameMS / 1000 // 640 byte
	ttsSrcRate  = 24000                           // OpenAI /audio/speech pcm = 24 kHz
	maxClipSec  = 20
	maxClipByte = pcmRate * 2 * maxClipSec
)

// ── Gom clip theo máy ───────────────────────────────────────────────────────

type clip struct {
	buf       bytes.Buffer
	listening bool
	started   time.Time
	cancelTTS context.CancelFunc // speak() đang chạy cho máy này, nếu có
}

var (
	clipsMu sync.Mutex
	clips   = map[string]*clip{}
)

func clipOf(id string) *clip {
	clipsMu.Lock()
	defer clipsMu.Unlock()
	c, ok := clips[id]
	if !ok {
		c = &clip{}
		clips[id] = c
	}
	return c
}

// handleListen: {"type":"listen","state":"start"|"stop"}
func handleListen(d *device, state string) {
	c := clipOf(d.id)
	clipsMu.Lock()
	defer clipsMu.Unlock()
	switch state {
	case "start":
		// Barge-in: người dùng nói khi máy đang phát → dừng bơm PCM ngay. Máy đã
		// tự xả đệm; nếu cloud cứ gửi tiếp thì máy lại phát câu cũ đè lên lúc thu.
		if c.cancelTTS != nil {
			c.cancelTTS()
			c.cancelTTS = nil
		}
		c.buf.Reset()
		c.listening = true
		c.started = time.Now()
		log.Printf("  🎤 %s bắt đầu nói", d.id)
	case "stop":
		if !c.listening {
			return
		}
		c.listening = false
		pcm := append([]byte(nil), c.buf.Bytes()...)
		c.buf.Reset()
		log.Printf("  🎤 %s nói xong: %.1fs", d.id, float64(len(pcm))/float64(pcmRate*2))
		if !*voiceMode {
			log.Printf("  (không -voice → bỏ clip)")
			return
		}
		go processClip(d, pcm) // ASR + LLM + TTS mất vài giây — không giữ goroutine WS
	}
}

// handleBinary: PCM từ máy khi đang listen.
func handleBinary(d *device, data []byte) {
	c := clipOf(d.id)
	clipsMu.Lock()
	defer clipsMu.Unlock()
	if !c.listening {
		return
	}
	if c.buf.Len()+len(data) > maxClipByte {
		return // trần: nút kẹt không thành clip vô tận
	}
	c.buf.Write(data)
}

// ── Xử lý một clip ──────────────────────────────────────────────────────────

func processClip(d *device, pcm []byte) {
	if len(pcm) < pcmFrameB*10 { // < 200ms = bấm nhầm
		log.Printf("  🎤 clip quá ngắn, bỏ")
		return
	}
	text, err := transcribe(pcm)
	if err != nil {
		log.Printf("  ✗ ASR: %v", err)
		speak(d, "Xin lỗi, tôi chưa nghe rõ.")
		return
	}
	log.Printf("  🎤 %s: \"%s\"", d.id, text)
	d.send(map[string]any{"type": "stt", "text": text})
	if strings.TrimSpace(text) == "" {
		return
	}
	// Câu nói → lượt hội thoại như một sự kiện thiết bị (aiLoop select).
	notifyDeviceEvent(d.id, "speech", json.RawMessage(fmt.Sprintf("%q", text)))
}

// speak: text → TTS → PCM 16k → máy, đúng nhịp thật. Huỷ được (barge-in):
// listen start của cùng máy sẽ cancel; tts stop vẫn được gửi để máy khép trạng thái.
func speak(d *device, text string) {
	if !*voiceMode || d == nil || strings.TrimSpace(text) == "" {
		return
	}
	pcm24, err := synthesize(text)
	if err != nil {
		log.Printf("  ✗ TTS: %v", err)
		return
	}
	pcm := resample24to16(pcm24)

	ctx, cancel := context.WithCancel(context.Background())
	c := clipOf(d.id)
	clipsMu.Lock()
	if c.cancelTTS != nil {
		c.cancelTTS() // câu mới thay câu cũ đang phát
	}
	c.cancelTTS = cancel
	clipsMu.Unlock()
	defer func() {
		clipsMu.Lock()
		if c.cancelTTS != nil {
			c.cancelTTS = nil
		}
		clipsMu.Unlock()
		cancel()
	}()

	log.Printf("  🔊 → %s: %.1fs \"%s\"", d.id, float64(len(pcm))/float64(pcmRate*2), text)
	d.send(map[string]any{"type": "tts", "state": "start"})
	// Gửi đúng 20ms mỗi 20ms: đệm phát của máy chỉ 1,5s — bắn cả clip một lúc
	// là tràn đệm và rơi tiếng.
	tick := time.NewTicker(pcmFrameMS * time.Millisecond)
	defer tick.Stop()
	for off := 0; off < len(pcm); off += pcmFrameB {
		end := off + pcmFrameB
		if end > len(pcm) {
			end = len(pcm)
		}
		d.sendBinary(pcm[off:end])
		select {
		case <-tick.C:
		case <-ctx.Done():
			log.Printf("  🔇 %s: bị ngắt (barge-in)", d.id)
			d.send(map[string]any{"type": "tts", "state": "stop"})
			return
		}
	}
	d.send(map[string]any{"type": "tts", "state": "stop"})
}

// ── Provider dạng OpenAI ────────────────────────────────────────────────────

func speechAuth() string {
	if *speechKey != "" {
		return *speechKey
	}
	return os.Getenv("OPENAI_API_KEY")
}

func transcribe(pcm []byte) (string, error) {
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	fw, _ := mw.CreateFormFile("file", "clip.wav")
	_, _ = fw.Write(wavWrap(pcm, pcmRate))
	_ = mw.WriteField("model", *asrModel)
	_ = mw.WriteField("language", *asrLang)
	_ = mw.WriteField("response_format", "json")
	_ = mw.Close()

	req, _ := http.NewRequest("POST", strings.TrimRight(*speechURL, "/")+"/audio/transcriptions", &body)
	req.Header.Set("Content-Type", mw.FormDataContentType())
	req.Header.Set("Authorization", "Bearer "+speechAuth())
	resp, err := (&http.Client{Timeout: 30 * time.Second}).Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(resp.Body)
	if resp.StatusCode/100 != 2 {
		return "", fmt.Errorf("ASR HTTP %d: %.200s", resp.StatusCode, raw)
	}
	var out struct {
		Text string `json:"text"`
	}
	if err := json.Unmarshal(raw, &out); err != nil {
		return "", fmt.Errorf("ASR trả JSON lạ: %.200s", raw)
	}
	return out.Text, nil
}

func synthesize(text string) ([]byte, error) {
	payload, _ := json.Marshal(map[string]any{
		"model": *ttsModel, "input": text, "voice": *ttsVoice, "response_format": "pcm",
	})
	req, _ := http.NewRequest("POST", strings.TrimRight(*speechURL, "/")+"/audio/speech", bytes.NewReader(payload))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+speechAuth())
	resp, err := (&http.Client{Timeout: 60 * time.Second}).Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(resp.Body)
	if resp.StatusCode/100 != 2 {
		return nil, fmt.Errorf("TTS HTTP %d: %.200s", resp.StatusCode, raw)
	}
	return raw, nil
}

// ── Tiện ích PCM ────────────────────────────────────────────────────────────

// wavWrap: PCM16 mono → WAV (44-byte header) để đưa cho ASR.
func wavWrap(pcm []byte, rate int) []byte {
	var b bytes.Buffer
	w := func(v any) { _ = binary.Write(&b, binary.LittleEndian, v) }
	b.WriteString("RIFF")
	w(uint32(36 + len(pcm)))
	b.WriteString("WAVEfmt ")
	w(uint32(16))
	w(uint16(1)) // PCM
	w(uint16(1)) // mono
	w(uint32(rate))
	w(uint32(rate * 2)) // byte rate
	w(uint16(2))        // block align
	w(uint16(16))       // bits
	b.WriteString("data")
	w(uint32(len(pcm)))
	b.Write(pcm)
	return b.Bytes()
}

// resample24to16: nội suy tuyến tính 24 kHz → 16 kHz (tỉ lệ 3:2). Đủ cho
// giọng nói; muốn đẹp hơn thì lọc thông thấp trước — không phải bây giờ.
func resample24to16(src []byte) []byte {
	n := len(src) / 2
	if n < 2 {
		return nil
	}
	outN := n * 2 / 3
	out := make([]byte, outN*2)
	for i := 0; i < outN; i++ {
		pos := float64(i) * 1.5
		j := int(pos)
		frac := pos - float64(j)
		if j+1 >= n {
			j = n - 2
			frac = 1
		}
		a := float64(int16(binary.LittleEndian.Uint16(src[j*2:])))
		c := float64(int16(binary.LittleEndian.Uint16(src[(j+1)*2:])))
		binary.LittleEndian.PutUint16(out[i*2:], uint16(int16(a+(c-a)*frac)))
	}
	return out
}
