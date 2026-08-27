// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include "gtek_config_store.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool unassigned;
    bool websocket_url_updated;
} gtek_ota_result_t;

esp_err_t gtek_ota_client_check_once(gtek_device_config_t *config, gtek_ota_result_t *result);

// Đăng ký busy-check: true = khách đang giao dịch → hoãn tải+reboot OTA
// (lần check sau thử lại). Board wiring (main) đăng ký; NULL = update ngay.
void gtek_ota_client_set_busy_check(bool (*fn)(void));

// gtek_ota_mark_valid xác nhận firmware vừa OTA chạy khỏe → huỷ rollback.
// Gọi sau khi WS kết nối thành công lần đầu. No-op nếu không ở trạng thái
// PENDING_VERIFY (boot bình thường, không phải vừa OTA).
void gtek_ota_mark_valid(void);

#ifdef __cplusplus
}
#endif
