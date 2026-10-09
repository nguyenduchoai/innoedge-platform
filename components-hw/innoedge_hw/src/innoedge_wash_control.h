// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Bộ điều khiển phiên rửa xe TỰ PHỤC VỤ THEO YÊU CẦU ──────────────────────
typedef enum {
    INNOEDGE_WASH_WATER = 0,
    INNOEDGE_WASH_FOAM = 1,
    INNOEDGE_WASH_AIR = 2,
    INNOEDGE_WASH_VACUUM = 3,
    INNOEDGE_WASH_DEVICE_COUNT = 4,
    INNOEDGE_WASH_NONE = -1,  // không thiết bị nào đang chạy
} innoedge_wash_device_t;


// Trạng thái phiên để UI đọc.
typedef struct {
    bool active;                                         // phiên đang mở?
    int remaining_sec[INNOEDGE_WASH_DEVICE_COUNT];       // ngân sách còn lại từng thiết bị
    innoedge_wash_device_t active_device;                // thiết bị đang chạy (hoặc NONE)
    int active_remaining_sec;                            // giây còn lại của thiết bị đang chạy
    int session_remaining_sec;                           // giây còn lại tới master deadline
} innoedge_wash_status_t;

// Khởi tạo controller: tạo mutex + task tick 1Hz, đảm bảo tắt hết relay.
esp_err_t innoedge_wash_control_init(void);

// Bắt đầu một phiên với ngân sách (giây) cho 4 thiết bị + trần thời gian cả phiên.
esp_err_t innoedge_wash_start(const int budgets_sec[INNOEDGE_WASH_DEVICE_COUNT], int master_max_sec);

// Khách bấm chức năng `device`: bật relay thiết bị đó (độc quyền).
esp_err_t innoedge_wash_activate(innoedge_wash_device_t device);

// Tắt relay thiết bị đang chạy, TẠM DỪNG (giữ ngân sách còn lại).
esp_err_t innoedge_wash_stop_active(void);

// Kết thúc phiên ngay: tắt hết relay, xoá trạng thái phiên.
esp_err_t innoedge_wash_end(void);

// Đọc trạng thái hiện tại (an toàn đa luồng).
bool innoedge_wash_status(innoedge_wash_status_t *status);

#ifdef __cplusplus
}
#endif
