// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include "ie_config_store.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t seq;
    char method[12];
    int coins;
    int64_t amount_vnd;
    int64_t rate_vnd;
    bool duplicate;
} ie_payment_ack_message_t;

typedef struct {
    uint64_t seq;
    int64_t amount;
    int64_t intent_id;
    int expires_sec;
    char ref_code[64];
    char qr_payload[512];
} ie_qr_message_t;

typedef struct {
    void (*on_ack)(uint64_t seq, void *ctx);
    void (*on_payment_ack)(const ie_payment_ack_message_t *ack, void *ctx);
    void (*on_command)(const char *command, const char *auth_token, const char *text, void *ctx);
    // Lệnh động (command bus): frame {"type":"command","commandId":N,"action":"...","params":{...}}.
    // params_json là chuỗi JSON của object "params" (có thể là "{}" hoặc rỗng).
    void (*on_dynamic_command)(int64_t command_id, const char *action,
                               const char *params_json, void *ctx);
    void (*on_status)(const char *state, void *ctx);
    void (*on_qr)(const ie_qr_message_t *qr, void *ctx);
    void (*on_qr_error)(uint64_t seq, const char *message, void *ctx);
    void (*on_payment_paid)(int64_t intent_id, int64_t amount, void *ctx);
    void (*on_static_qr)(const char *payload, const char *ref_code, void *ctx);
    // Khung binary (audio PCM, ảnh…). Có thể là một mảnh của frame lớn — với
    // dữ liệu dòng (PCM) mảnh vẫn là dữ liệu hợp lệ. Chạy trên task WS: ngắn thôi.
    void (*on_binary)(const uint8_t *data, size_t len, void *ctx);
    // Khung text có "type" mà SDK không biết (vd "tts","stt") → application.
    // raw là JSON nguyên văn, sống tới khi callback trả về.
    void (*on_frame)(const char *type, const char *raw, void *ctx);
    void *ctx;
} ie_ws_handlers_t;

esp_err_t ie_ws_client_start(const ie_device_config_t *config,
                               const ie_ws_handlers_t *handlers);
esp_err_t ie_ws_client_stop(void);
bool ie_ws_client_is_connected(void);
esp_err_t ie_ws_client_send_text(const char *text);
// Gửi một frame binary (opcode 0x02). Thread-safe như send_text.
esp_err_t ie_ws_client_send_binary(const uint8_t *data, size_t len);
esp_err_t ie_ws_client_send_heartbeat(const char *fw_version, int rssi,
                                        unsigned queue_depth, int reset_reason);
esp_err_t ie_ws_client_request_qr(uint64_t seq, int64_t amount);
// Gửi ack cho lệnh động: {"type":"command_ack","commandId":N,"status":"ok"|"error",
// "message":"...","result":{...}}. status bắt buộc; message/result_json có thể NULL.
// result_json phải là chuỗi JSON object hợp lệ (vd "{\"pulses\":3}") — NULL/rỗng bỏ field.
esp_err_t ie_ws_client_send_command_ack(int64_t command_id, const char *status,
                                          const char *message, const char *result_json);

// Gửi cảnh báo lên server:
//   {"type":"alert","code":"...","severity":"...","message":"...","active":bool}
// code bắt buộc (vd "no_water","pump_error","fault"); severity vd "info"/"warning"/
// "error" (NULL → "warning"); message có thể NULL; active=true là cảnh báo đang xảy
// ra, false là đã hết (clear). An toàn gọi khi WS chưa kết nối (trả lỗi, không crash).
esp_err_t ie_ws_send_alert(const char *code, const char *severity,
                             const char *message, bool active);

#ifdef __cplusplus
}
#endif
