// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

// Bộ phát cảnh báo lỗi tập trung cho firmware G-TEK. Mục tiêu: máy KHÔNG chạy âm
// thầm khi hỏng — mọi điều kiện lỗi gọi gtek_fault_set() để đẩy cảnh báo lên
// server (qua gtek_ws_send_alert). Có latch theo code: chỉ gửi khi CHUYỂN trạng
// thái (lần đầu bật / khi clear) → không spam server dù gọi trong vòng lặp.
//
// Dùng cho LỖI TRẠNG THÁI (có lúc hết): set khi vào lỗi, clear khi thoát.
// Với LỖI SỰ KIỆN một lần (persist fail, init fail) chỉ cần gọi set().

#ifdef __cplusplus
extern "C" {
#endif

// gtek_fault_set: báo lỗi code đang xảy ra. severity NULL → "warning". Chỉ thật
// sự gửi frame nếu code chưa active (chống lặp).
void gtek_fault_set(const char *code, const char *severity, const char *message);

// gtek_fault_clear: báo lỗi code đã hết. Chỉ gửi nếu code đang active.
void gtek_fault_clear(const char *code);

#ifdef __cplusplus
}
#endif
