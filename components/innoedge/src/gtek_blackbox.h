// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTEK_BLACKBOX_MAX_BREADCRUMBS 8
#define GTEK_BLACKBOX_TAG_LEN         16
#define GTEK_BLACKBOX_INFO_LEN        48

typedef struct {
    char tag[GTEK_BLACKBOX_TAG_LEN];
    char info[GTEK_BLACKBOX_INFO_LEN];
    uint32_t uptime_sec;
} gtek_breadcrumb_t;

// Khởi tạo hộp đen, đọc nguyên nhân reboot trước đó và nạp báo cáo chẩn đoán nếu có sự cố.
esp_err_t gtek_blackbox_init(void);

// Ghi lại một vết vận hành (breadcrumb) vào bộ nhớ đệm tròn (an toàn, không malloc).
void gtek_blackbox_record_breadcrumb(const char *tag, const char *info);

// Ghi nhận lỗi nghiêm trọng vào NVS (lưu lại qua reboot).
esp_err_t gtek_blackbox_record_error(const char *code, const char *details);

// Kiểm tra xem có báo cáo sự cố (crash/watchdog/brownout) chưa được gửi lên cloud hay không.
bool gtek_blackbox_has_pending_report(void);

// Xuất báo cáo chẩn đoán thành chuỗi JSON để gửi lên cloud.
esp_err_t gtek_blackbox_format_report(char *out, size_t max_len);

// Xoá cờ báo cáo sự cố sau khi đã gửi thành công lên cloud.
void gtek_blackbox_clear_report(void);

// Lấy danh sách vết vận hành gần nhất (dành cho kiểm thử/chẩn đoán cục bộ).
size_t gtek_blackbox_get_breadcrumbs(gtek_breadcrumb_t *out_arr, size_t max_count);

#ifdef __cplusplus
}
#endif
