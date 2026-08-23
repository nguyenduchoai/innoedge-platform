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
//
// Một combo = NGÂN SÁCH THỜI GIAN (giây) cho từng thiết bị. 4 thiết bị:
// water (xịt nước), foam (phun bọt), air (nén khí), vacuum (hút bụi).
// Khách bấm một chức năng → relay đó BẬT và đếm ngược ngân sách CỦA NÓ; tại một
// thời điểm chỉ MỘT relay bật (bảo vệ bơm); ngân sách của thiết bị nào về 0 thì
// chức năng đó bị khoá; khi mọi ngân sách = 0 HOẶC qua hạn chót phiên (master
// deadline) → phiên kết thúc (tắt hết relay, về idle).

// Thiết bị rửa. Thứ tự khớp index mảng budgets_sec[4] và gtek_relay_channel_t
// (WATER/FOAM/AIR/VACUUM nằm đầu enum relay).
typedef enum {
    GTEK_WASH_WATER = 0,
    GTEK_WASH_FOAM = 1,
    GTEK_WASH_AIR = 2,
    GTEK_WASH_VACUUM = 3,
    GTEK_WASH_DEVICE_COUNT = 4,
    GTEK_WASH_NONE = -1,  // không thiết bị nào đang chạy
} gtek_wash_device_t;

// Trạng thái phiên để UI đọc.
typedef struct {
    bool active;                                 // phiên đang mở?
    int remaining_sec[GTEK_WASH_DEVICE_COUNT];   // ngân sách còn lại từng thiết bị
    gtek_wash_device_t active_device;            // thiết bị đang chạy (hoặc NONE)
    int active_remaining_sec;                    // giây còn lại của thiết bị đang chạy
    int session_remaining_sec;                   // giây còn lại tới master deadline
} gtek_wash_status_t;

// Khởi tạo controller: tạo mutex + task tick 1Hz, đảm bảo tắt hết relay. Gọi một
// lần khi khởi động (sau gtek_relay_control_init).
esp_err_t gtek_wash_control_init(void);

// Bắt đầu một phiên với ngân sách (giây) cho 4 thiết bị + trần thời gian cả phiên
// (master_max_sec). budgets_sec dài GTEK_WASH_DEVICE_COUNT. Ngân sách <=0 nghĩa
// là thiết bị đó không có trong combo (bị khoá). master_max_sec<=0 → tự suy ra
// = tổng ngân sách (cộng biên). Phiên mới ghi đè phiên cũ; mọi relay tắt trước.
esp_err_t gtek_wash_start(const int budgets_sec[GTEK_WASH_DEVICE_COUNT], int master_max_sec);

// Khách bấm chức năng `device`: bật relay thiết bị đó (độc quyền), đặt làm thiết
// bị đang chạy. Bỏ qua nếu không có phiên, ngân sách thiết bị đó = 0, hoặc device
// không hợp lệ. Bấm lại đúng thiết bị đang chạy → giữ nguyên (no-op).
esp_err_t gtek_wash_activate(gtek_wash_device_t device);

// Tắt relay thiết bị đang chạy, TẠM DỪNG (giữ ngân sách còn lại). Phiên vẫn mở.
esp_err_t gtek_wash_stop_active(void);

// Kết thúc phiên ngay: tắt hết relay, xoá trạng thái phiên.
esp_err_t gtek_wash_end(void);

// Đọc trạng thái hiện tại (an toàn đa luồng). status có thể NULL để chỉ hỏi
// "phiên có đang mở không" qua giá trị trả về.
bool gtek_wash_status(gtek_wash_status_t *status);

#ifdef __cplusplus
}
#endif
