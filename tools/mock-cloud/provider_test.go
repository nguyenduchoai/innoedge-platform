// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Test hai provider tiếng Việt (mặc định): Qwen3-ASR và VieNeu TTS — kiểm đúng
// hợp đồng đã chạy thật bên VIMATE Edu, không cần server thật.
package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestMacDinhLaQwen3VaVieNeu(t *testing.T) {
	if *asrKind != "qwen3" || *ttsKind != "vieneu" {
		t.Errorf("mặc định phải là qwen3 + vieneu (tiếng Việt), đang là %s + %s", *asrKind, *ttsKind)
	}
	ab, _, m := asrConfig()
	if ab != "http://localhost:8000/v1" || m != "Qwen/Qwen3-ASR-1.7B" {
		t.Errorf("qwen3 mặc định phải là self-host tools/voice-stack, đang là %s %s", ab, m)
	}
	if qwenAudioField() != "audio_url" {
		t.Error("self-host vLLM phải dùng audio_url")
	}
	b, _, tm, v := ttsConfig()
	if b != "http://localhost:8080/v1" || tm != "vieneu-v3-turbo" || v != "Phạm Tuyên" {
		t.Errorf("vieneu mặc định sai: %s %s %q", b, tm, v)
	}
}

// Qwen3-ASR: audio đi trong content-part của /chat/completions — KHÔNG phải
// multipart /audio/transcriptions như Whisper.
func TestQwen3ASRDungHopDongChatCompletions(t *testing.T) {
	var gotPath string
	var gotBody map[string]any
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.Path
		raw, _ := io.ReadAll(r.Body)
		_ = json.Unmarshal(raw, &gotBody)
		w.Header().Set("Content-Type", "application/json")
		// Model bọc kết quả trong <asr_text>, kèm lời dẫn và một chữ Hán lọt.
		_, _ = w.Write([]byte(`{"choices":[{"message":{"content":"Đây là kết quả: <asr_text>bật đèn 你好 lên</asr_text>。"}}]}`))
	}))
	defer srv.Close()

	oK, oU, oKey := *asrKind, *asrURL, *asrKey
	*asrKind, *asrURL, *asrKey = "dashscope", srv.URL, "k"
	defer func() { *asrKind, *asrURL, *asrKey = oK, oU, oKey }()

	text, err := transcribe(make([]byte, pcmFrameB*20))
	if err != nil {
		t.Fatal(err)
	}
	if gotPath != "/chat/completions" {
		t.Errorf("gọi %s, Qwen3 phải là /chat/completions", gotPath)
	}
	if gotBody["model"] != "qwen3-asr-flash" {
		t.Errorf("model = %v", gotBody["model"])
	}
	msgs := gotBody["messages"].([]any)
	parts := msgs[0].(map[string]any)["content"].([]any)
	audio := parts[0].(map[string]any)
	if audio["type"] != "input_audio" {
		t.Errorf("DashScope cần content-part input_audio, có %v", audio["type"])
	}
	ia := audio["input_audio"].(map[string]any)
	if ia["format"] != "wav" || !strings.HasPrefix(ia["data"].(string), "data:audio/wav;base64,") {
		t.Error("audio phải là WAV base64 data URI")
	}
	if !strings.Contains(parts[1].(map[string]any)["text"].(string), "tiếng Việt") {
		t.Error("thiếu instruction tiếng Việt — model sẽ trả chữ Hán")
	}
	if opts := gotBody["asr_options"].(map[string]any); opts["language"] != "vi" {
		t.Errorf("asr_options.language = %v", opts["language"])
	}
	// Dọn kết quả: bỏ tag, bỏ lời dẫn, bỏ chữ Hán, bỏ dấu câu cuối.
	if text != "bật đèn lên" {
		t.Errorf("text sau dọn = %q, mong %q", text, "bật đèn lên")
	}
}

func TestQwen3SelfHostDungAudioURL(t *testing.T) {
	var gotType string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var b map[string]any
		_ = json.NewDecoder(r.Body).Decode(&b)
		parts := b["messages"].([]any)[0].(map[string]any)["content"].([]any)
		gotType = parts[0].(map[string]any)["type"].(string)
		_, _ = w.Write([]byte(`{"choices":[{"message":{"content":[{"type":"text","text":"xin chào"}]}}]}`))
	}))
	defer srv.Close()
	oK, oU := *asrKind, *asrURL
	*asrKind, *asrURL = "qwen3", srv.URL // self-host → tự dùng audio_url
	defer func() { *asrKind, *asrURL = oK, oU }()

	text, err := transcribe(make([]byte, pcmFrameB*20))
	if err != nil {
		t.Fatal(err)
	}
	if gotType != "audio_url" {
		t.Errorf("vLLM self-host cần audio_url, gửi %s", gotType)
	}
	if text != "xin chào" {
		t.Errorf("content dạng mảng part không được parse: %q", text)
	}
}

func TestCleanQwenText(t *testing.T) {
	cases := map[string]string{
		"<asr_text>bật đèn</asr_text>":           "bật đèn",
		"Kết quả: <asr_text>tắt đèn.</asr_text>": "tắt đèn",
		"你好 xin chào 世界":                         "xin chào",
		"  chạy motor  ":                         "chạy motor",
		"":                                       "",
	}
	for in, want := range cases {
		if got := cleanQwenText(in); got != want {
			t.Errorf("clean(%q) = %q, mong %q", in, got, want)
		}
	}
}

// VieNeu: xin thẳng 16 kHz (không resample), kiểm header như VIMATE.
func TestVieNeuXin16kVaKiemHeader(t *testing.T) {
	var gotBody map[string]any
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewDecoder(r.Body).Decode(&gotBody)
		if r.Header.Get("Authorization") != "Bearer vk" {
			http.Error(w, "401", 401)
			return
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Header().Set("X-Audio-Format", "pcm_s16le")
		w.Header().Set("X-Audio-Sample-Rate", "16000")
		w.Header().Set("X-Audio-Channels", "1")
		_, _ = w.Write(make([]byte, 3200)) // 100 ms @16k
	}))
	defer srv.Close()
	oK, oU, oKey := *ttsKind, *ttsURL, *ttsKey
	*ttsKind, *ttsURL, *ttsKey = "vieneu", srv.URL, "vk"
	defer func() { *ttsKind, *ttsURL, *ttsKey = oK, oU, oKey }()

	pcm, err := synthesize("Xin chào con")
	if err != nil {
		t.Fatal(err)
	}
	if gotBody["sample_rate"] != float64(16000) {
		t.Errorf("phải xin sample_rate=16000 để khỏi resample, gửi %v", gotBody["sample_rate"])
	}
	if gotBody["voice"] != "Phạm Tuyên" || gotBody["style"] != "tu_nhien" || gotBody["response_format"] != "pcm" {
		t.Errorf("request thiếu voice/style/pcm: %v", gotBody)
	}
	if len(pcm) != 3200 {
		t.Errorf("PCM về %d byte — bị resample nhầm (VieNeu đã trả 16k)", len(pcm))
	}
}

func TestVieNeuTuChoiSampleRateSai(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Audio-Sample-Rate", "24000") // sidecar cấu hình sai
		_, _ = w.Write(make([]byte, 4800))
	}))
	defer srv.Close()
	oK, oU := *ttsKind, *ttsURL
	*ttsKind, *ttsURL = "vieneu", srv.URL
	defer func() { *ttsKind, *ttsURL = oK, oU }()

	if _, err := synthesize("a"); err == nil || !strings.Contains(err.Error(), "24000") {
		t.Errorf("sidecar trả sai sample rate phải bị từ chối rõ ràng, nhận: %v", err)
	}
}

func TestVoiceReadyBaoThieuKeyRo(t *testing.T) {
	oK, oKey, oU := *asrKind, *asrKey, *asrURL
	*asrKind, *asrKey, *asrURL = "dashscope", "", ""
	defer func() { *asrKind, *asrKey, *asrURL = oK, oKey, oU }()
	t.Setenv("DASHSCOPE_API_KEY", "")
	if err := voiceReady(); err == nil || !strings.Contains(err.Error(), "DASHSCOPE") {
		t.Errorf("thiếu key dashscope phải báo tên biến, nhận: %v", err)
	}
	*asrKind = "qwen3" // self-host mặc định → không đòi key
	t.Setenv("QWEN3_ASR_API_KEY", "")
	if err := voiceReady(); err != nil {
		t.Errorf("qwen3 self-host không được đòi key: %v", err)
	}
}
