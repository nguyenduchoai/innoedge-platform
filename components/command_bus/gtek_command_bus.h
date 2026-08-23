// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Command bus: cơ chế LỆNH ĐỘNG ──────────────────────────────────────────
//
// Server gửi:  {"type":"command","commandId":N,"action":"<tên>","params":{...}}
// Thiết bị ack: {"type":"command_ack","commandId":N,"status":"ok"|"error",
//                "message":"...","result":{...}}
//
// Thêm một nghiệp vụ mới = viết một handler + thêm 1 dòng vào bảng registry
// (xem gtek_command_bus.c). KHÔNG cần sửa dispatcher.
//
// Handler nhận params (cJSON, có thể NULL nếu lệnh không kèm params) và ghi
// kết quả vào 2 buffer do dispatcher cấp:
//   - result_out: chuỗi JSON object cho field "result" (vd "{\"pulses\":3}").
//                 Để rỗng nếu không có. KHÔNG bao gồm field "result" bao ngoài.
//   - msg_out:    thông điệp người-đọc cho field "message".
// Trả ESP_OK → ack status "ok"; trả lỗi khác → ack status "error" (msg_out dùng
// làm message; nếu để rỗng dispatcher tự điền theo mã lỗi).
typedef esp_err_t (*gtek_command_handler_fn)(cJSON *params,
                                             char *result_out, size_t result_len,
                                             char *msg_out, size_t msg_len);

typedef struct {
    const char *action;
    gtek_command_handler_fn handler;
} gtek_command_entry_t;

// Khởi tạo (nạp commandId cuối từ NVS vào bộ dedupe). Gọi một lần khi khởi động,
// TRƯỚC khi đăng ký handler.
esp_err_t gtek_command_bus_init(void);

// Đăng ký một handler cho action. `action` phải là chuỗi sống lâu (string
// literal / static) — registry chỉ giữ con trỏ, KHÔNG copy. Đăng ký lại cùng
// action = ghi đè handler cũ. Tối đa GTEK_CMD_REGISTRY_MAX (24) action.
esp_err_t gtek_command_bus_register(const char *action, gtek_command_handler_fn handler);

// Yêu cầu reboot TRỄ (~800ms) để dispatcher kịp gửi command_ack trước khi máy
// khởi động lại. Dùng trong handler "reboot".
void gtek_command_bus_request_reboot(void);

// Xử lý một lệnh động: dedupe theo command_id → lookup action trong registry →
// gọi handler → GỬI command_ack qua ws_client. Lệnh trùng chỉ ack lại
// (status "ok", message "duplicate"), KHÔNG gọi handler. action lạ → ack
// status "error", message "unknown action".
// params_json là chuỗi JSON của object params ("{}" nếu rỗng); có thể NULL.
void gtek_command_bus_dispatch(int64_t command_id, const char *action,
                               const char *params_json);

#ifdef __cplusplus
}
#endif
