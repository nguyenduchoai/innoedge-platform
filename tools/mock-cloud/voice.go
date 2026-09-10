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
	"encoding/base64"
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

	// ASR — mặc định Qwen3-ASR (tiếng Việt tốt, cùng provider với VIMATE Edu).
	asrKind  = flag.String("asr", "qwen3", "ASR: qwen3 (DashScope hoặc vLLM self-host) | whisper (API dạng OpenAI)")
	asrURL   = flag.String("asr-url", "", "base URL ASR (rỗng = mặc định theo -asr)")
	asrKey   = flag.String("asr-key", "", "API key ASR (rỗng = $DASHSCOPE_API_KEY cho qwen3, $OPENAI_API_KEY cho whisper)")
	asrModel = flag.String("asr-model", "", "model ASR (rỗng = qwen3-asr-flash | whisper-1)")
	asrLang  = flag.String("asr-lang", "vi", "ngôn ngữ ASR (ISO-639-1)")
	asrAudio = flag.String("asr-audio-field", "input_audio", "qwen3: input_audio (DashScope) | audio_url (vLLM self-host)")

	// TTS — mặc định VieNeu (giọng Việt tự nhiên, sidecar self-host của VIMATE Edu).
	ttsKind  = flag.String("tts", "vieneu", "TTS: vieneu (sidecar self-host) | openai (API dạng OpenAI)")
	ttsURL   = flag.String("tts-url", "", "base URL TTS (rỗng = http://localhost:8080/v1 cho vieneu, api.openai.com cho openai)")
	ttsKey   = flag.String("tts-key", "", "API key TTS (rỗng = $VIENEU_TTS_API_KEY | $OPENAI_API_KEY)")
	ttsModel = flag.String("tts-model", "", "model TTS (rỗng = vieneu-v3-turbo | tts-1)")
	ttsVoice = flag.String("tts-voice", "", "giọng TTS (rỗng = \"Phạm Tuyên\" | nova)")
	ttsStyle = flag.String("tts-style", "tu_nhien", "vieneu: phong cách giọng")
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
	pcm, err := synthesize(text) // đã là PCM16 mono 16 kHz
	if err != nil {
		log.Printf("  ✗ TTS: %v", err)
		return
	}

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

// ── Provider ────────────────────────────────────────────────────────────────
// Hợp đồng lấy từ VIMATE Edu (Go-Xiaozhi/server/internal/voice): asr_qwen3.go
// và tts_vieneu.go — đã chạy thật với trẻ em nói tiếng Việt.

func envOr(flagVal, env string) string {
	if flagVal != "" {
		return flagVal
	}
	return os.Getenv(env)
}

func asrConfig() (base, key, model string) {
	switch *asrKind {
	case "whisper":
		base = strings.TrimRight(*asrURL, "/")
		if base == "" {
			base = "https://api.openai.com/v1"
		}
		model = *asrModel
		if model == "" {
			model = "whisper-1"
		}
		return base, envOr(*asrKey, "OPENAI_API_KEY"), model
	default: // qwen3
		base = strings.TrimRight(*asrURL, "/")
		if base == "" {
			base = "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"
		}
		model = *asrModel
		if model == "" {
			model = "qwen3-asr-flash"
		}
		return base, envOr(*asrKey, "DASHSCOPE_API_KEY"), model
	}
}

func ttsConfig() (base, key, model, voice string) {
	switch *ttsKind {
	case "openai":
		base = strings.TrimRight(*ttsURL, "/")
		if base == "" {
			base = "https://api.openai.com/v1"
		}
		model, voice = *ttsModel, *ttsVoice
		if model == "" {
			model = "tts-1"
		}
		if voice == "" {
			voice = "nova"
		}
		return base, envOr(*ttsKey, "OPENAI_API_KEY"), model, voice
	default: // vieneu
		base = strings.TrimRight(*ttsURL, "/")
		if base == "" {
			base = "http://localhost:8080/v1"
		}
		model, voice = *ttsModel, *ttsVoice
		if model == "" {
			model = "vieneu-v3-turbo"
		}
		if voice == "" {
			voice = "Phạm Tuyên"
		}
		return base, envOr(*ttsKey, "VIENEU_TTS_API_KEY"), model, voice
	}
}

// voiceReady kiểm cấu hình lúc khởi động — thiếu key thì fail sớm, rõ ràng.
func voiceReady() error {
	_, akey, _ := asrConfig()
	if *asrKind == "whisper" && akey == "" {
		return fmt.Errorf("-asr whisper cần -asr-key hoặc $OPENAI_API_KEY")
	}
	if *asrKind == "qwen3" && akey == "" && *asrURL == "" {
		return fmt.Errorf("-asr qwen3 cần -asr-key hoặc $DASHSCOPE_API_KEY (self-host vLLM thì đặt -asr-url)")
	}
	_, tkey, _, _ := ttsConfig()
	if *ttsKind == "openai" && tkey == "" {
		return fmt.Errorf("-tts openai cần -tts-key hoặc $OPENAI_API_KEY")
	}
	return nil // vieneu: key tuỳ sidecar; không có thì sidecar tự báo 401
}

func transcribe(pcm []byte) (string, error) {
	if *asrKind == "whisper" {
		return transcribeWhisper(pcm)
	}
	return transcribeQwen3(pcm)
}

func synthesize(text string) ([]byte, error) {
	if *ttsKind == "openai" {
		pcm24, err := synthesizeOpenAI(text)
		if err != nil {
			return nil, err
		}
		return resample24to16(pcm24), nil
	}
	return synthesizeVieNeu(text)
}

// ── Qwen3-ASR: audio đi trong content-part của /chat/completions ────────────
// Khác Whisper hoàn toàn. Model có thể bọc kết quả trong <asr_text>…</asr_text>
// và đôi khi trả chữ Hán — cleanQwenText xử lý giống VIMATE.

const qwen3VietnameseInstruction = "Nhận dạng âm thanh bằng tiếng Việt. Chỉ trả transcript tiếng Việt dạng chữ Latin/Quốc ngữ, không trả tiếng Trung hoặc chữ Hán."

func transcribeQwen3(pcm []byte) (string, error) {
	base, key, model := asrConfig()
	dataURI := "data:audio/wav;base64," + base64.StdEncoding.EncodeToString(wavWrap(pcm, pcmRate))
	var audioPart map[string]any
	if *asrAudio == "audio_url" { // vLLM self-host
		audioPart = map[string]any{"type": "audio_url", "audio_url": map[string]any{"url": dataURI}}
	} else { // DashScope compatible-mode
		audioPart = map[string]any{"type": "input_audio", "input_audio": map[string]any{"data": dataURI, "format": "wav"}}
	}
	body := map[string]any{
		"model": model,
		"messages": []map[string]any{{
			"role":    "user",
			"content": []map[string]any{audioPart, {"type": "text", "text": qwen3VietnameseInstruction}},
		}},
		"stream":      false,
		"asr_options": map[string]any{"language": *asrLang, "enable_itn": false},
	}
	payload, _ := json.Marshal(body)
	req, _ := http.NewRequest("POST", base+"/chat/completions", bytes.NewReader(payload))
	req.Header.Set("Content-Type", "application/json")
	if key != "" {
		req.Header.Set("Authorization", "Bearer "+key)
	}
	resp, err := (&http.Client{Timeout: 30 * time.Second}).Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	if resp.StatusCode/100 != 2 {
		return "", fmt.Errorf("Qwen3-ASR HTTP %d: %.200s", resp.StatusCode, raw)
	}
	var out struct {
		Choices []struct {
			Message struct {
				Content json.RawMessage `json:"content"`
			} `json:"message"`
		} `json:"choices"`
		Error *struct {
			Message string `json:"message"`
		} `json:"error"`
	}
	if err := json.Unmarshal(raw, &out); err != nil {
		return "", fmt.Errorf("Qwen3-ASR trả JSON lạ: %.200s", raw)
	}
	if out.Error != nil {
		return "", fmt.Errorf("Qwen3-ASR: %s", out.Error.Message)
	}
	if len(out.Choices) == 0 {
		return "", fmt.Errorf("Qwen3-ASR không có choices")
	}
	return cleanQwenText(qwenContentText(out.Choices[0].Message.Content)), nil
}

// content là string hoặc mảng content-part [{type,text}].
func qwenContentText(raw json.RawMessage) string {
	var s string
	if json.Unmarshal(raw, &s) == nil {
		return s
	}
	var parts []struct {
		Text string `json:"text"`
	}
	if json.Unmarshal(raw, &parts) == nil {
		var b strings.Builder
		for _, p := range parts {
			b.WriteString(p.Text)
		}
		return b.String()
	}
	return ""
}

func cleanQwenText(s string) string {
	s = strings.TrimSpace(s)
	if i := strings.Index(s, "<asr_text>"); i >= 0 {
		s = s[i+len("<asr_text>"):]
		if j := strings.LastIndex(s, "</asr_text>"); j >= 0 {
			s = s[:j]
		}
	}
	s = strings.ReplaceAll(s, "<asr_text>", "")
	s = strings.ReplaceAll(s, "</asr_text>", "")
	s = strings.Trim(strings.TrimSpace(s), " \t\r\n。！？!?.,，、;；:\"“”'‘’")
	// Chữ Hán lọt vào (model song ngữ) → bỏ, giữ phần Quốc ngữ.
	var b strings.Builder
	for _, r := range s {
		if r >= 0x4E00 && r <= 0x9FFF {
			continue
		}
		b.WriteRune(r)
	}
	return strings.Join(strings.Fields(b.String()), " ")
}

// ── VieNeu TTS: sidecar self-host, trả PCM16 mono ở sample_rate ta xin ───────
// Xin thẳng 16 kHz → không resample. Kiểm header như VIMATE để một sidecar sai
// cấu hình không thành tiếng rè khó hiểu.

func synthesizeVieNeu(text string) ([]byte, error) {
	base, key, model, voice := ttsConfig()
	payload, _ := json.Marshal(map[string]any{
		"model": model, "input": text, "voice": voice, "style": *ttsStyle,
		"response_format": "pcm", "sample_rate": pcmRate,
	})
	req, _ := http.NewRequest("POST", base+"/audio/speech", bytes.NewReader(payload))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/octet-stream")
	if key != "" {
		req.Header.Set("Authorization", "Bearer "+key)
	}
	resp, err := (&http.Client{Timeout: 75 * time.Second}).Do(req)
	if err != nil {
		return nil, fmt.Errorf("VieNeu: %w (sidecar có chạy ở %s không?)", err, base)
	}
	defer resp.Body.Close()
	if resp.StatusCode/100 != 2 {
		raw, _ := io.ReadAll(io.LimitReader(resp.Body, 8<<10))
		return nil, fmt.Errorf("VieNeu HTTP %d: %.200s", resp.StatusCode, raw)
	}
	if f := resp.Header.Get("X-Audio-Format"); f != "" && f != "pcm_s16le" {
		return nil, fmt.Errorf("VieNeu trả format %q, cần pcm_s16le", f)
	}
	if r := resp.Header.Get("X-Audio-Sample-Rate"); r != "" && r != fmt.Sprint(pcmRate) {
		return nil, fmt.Errorf("VieNeu trả %s Hz, đã xin %d", r, pcmRate)
	}
	if c := resp.Header.Get("X-Audio-Channels"); c != "" && c != "1" {
		return nil, fmt.Errorf("VieNeu trả %s kênh, cần mono", c)
	}
	pcm, err := io.ReadAll(io.LimitReader(resp.Body, int64(maxClipByte)*3))
	if err != nil {
		return nil, err
	}
	if len(pcm) == 0 || len(pcm)%2 != 0 {
		return nil, fmt.Errorf("VieNeu trả PCM %d byte (rỗng hoặc lẻ)", len(pcm))
	}
	return pcm, nil
}

// ── Whisper / OpenAI TTS: API dạng OpenAI, chạy được cả server local ────────

func transcribeWhisper(pcm []byte) (string, error) {
	base, key, model := asrConfig()
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	fw, _ := mw.CreateFormFile("file", "clip.wav")
	_, _ = fw.Write(wavWrap(pcm, pcmRate))
	_ = mw.WriteField("model", model)
	_ = mw.WriteField("language", *asrLang)
	_ = mw.WriteField("response_format", "json")
	_ = mw.Close()

	req, _ := http.NewRequest("POST", base+"/audio/transcriptions", &body)
	req.Header.Set("Content-Type", mw.FormDataContentType())
	req.Header.Set("Authorization", "Bearer "+key)
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

func synthesizeOpenAI(text string) ([]byte, error) {
	base, key, model, voice := ttsConfig()
	payload, _ := json.Marshal(map[string]any{
		"model": model, "input": text, "voice": voice, "response_format": "pcm",
	})
	req, _ := http.NewRequest("POST", base+"/audio/speech", bytes.NewReader(payload))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+key)
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
