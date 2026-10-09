// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include "ie_config_store.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool unassigned;
    bool websocket_url_updated;
    bool deferred; // có bản mới nhưng máy đang bận → nên hỏi lại sớm
} ie_ota_result_t;

// Hỏi cloud bản mới; có thì tải, kiểm size + SHA-256 + mô tả app, rồi reboot.
// Manifest: {"firmware":{"version":"X.Y.Z","url":"https://...","sha256":"<64 hex>",
//            "size":<byte>,"allowDowngrade":false}} — thiếu sha256/size = từ chối.
esp_err_t ie_ota_client_check_once(ie_device_config_t *config, ie_ota_result_t *result);

// Phiên bản firmware đang chạy — MỘT nguồn cho hello, heartbeat, OTA.
// Mặc định: version trong mô tả app (PROJECT_VER). ie_fw_version_set(chuỗi) ghi
// đè (dành cho Arduino/PlatformIO); khi đó SDK không đối chiếu version trong
// image tải về được nữa, người dùng tự giữ hai con số khớp nhau.
const char *ie_fw_version(void);
void ie_fw_version_set(const char *version);

// Đăng ký busy-check: true = khách đang giao dịch → hoãn tải+reboot OTA
// (lần check sau thử lại). Board wiring (main) đăng ký; NULL = update ngay.
void ie_ota_client_set_busy_check(bool (*fn)(void));

// ie_ota_mark_valid xác nhận firmware vừa OTA chạy khỏe → huỷ rollback.
// Gọi sau khi WS kết nối thành công lần đầu. No-op nếu không ở trạng thái
// PENDING_VERIFY (boot bình thường, không phải vừa OTA).
void ie_ota_mark_valid(void);

#ifdef __cplusplus
}
#endif
