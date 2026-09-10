// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once
//
// innoedge_audio — micro/loa I2S thuần + kênh audio hai chiều với cloud.
//
// Driver PHẦN CỨNG MẪU, không thuộc SDK core. Chạy với mic MEMS I2S (INMP441,
// SPH0645) và amp I2S (MAX98357A, PCM5102) — bộ rẻ nhất có thể mua. Board có
// codec chip (ES8311…) cần driver riêng, không dùng file này.
//
// Định dạng cố định: PCM16 mono 16 kHz, cả hai chiều. Không Opus — trên LAN
// 32 KB/s là chuyện nhỏ; nén khi ra internet, không phải bây giờ.
//
// Giao thức (xem docs/PROTOCOL-v1.md §9):
//   máy → cloud : {"type":"listen","state":"start","format":"pcm16","rate":16000}
//                 <binary PCM 20ms/khung> …  {"type":"listen","state":"stop"}
//   cloud → máy : {"type":"stt","text":"…"}                 (máy nghe được gì)
//                 {"type":"tts","state":"start"} <binary PCM> {"type":"tts","state":"stop"}
//
// Nối vào SDK: đặt innoedge_audio_on_frame / on_binary vào innoedge_events_t.

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_stt)(const char *text);        // cloud đã nhận dạng câu nói của người dùng
    void (*on_tts)(bool playing);            // bắt đầu / kết thúc phát trả lời
} innoedge_audio_events_t;

// Khởi tạo I2S theo Kconfig (GPIO -1 = tắt kênh đó). Gọi SAU innoedge_init().
esp_err_t innoedge_audio_init(const innoedge_audio_events_t *ev);

// Push-to-talk: bắt đầu thu mic và stream lên cloud; dừng = gửi listen stop.
esp_err_t innoedge_audio_listen_start(void);
esp_err_t innoedge_audio_listen_stop(void);
bool innoedge_audio_is_listening(void);
bool innoedge_audio_is_playing(void);

// Cắm vào innoedge_events_t.on_frame / .on_binary.
void innoedge_audio_on_frame(const char *type, const char *raw_json);
void innoedge_audio_on_binary(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
