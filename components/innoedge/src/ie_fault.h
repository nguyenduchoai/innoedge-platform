// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

// Bộ phát cảnh báo lỗi tập trung của SDK. Mục tiêu: máy KHÔNG chạy âm
// thầm khi hỏng — mọi điều kiện lỗi gọi ie_fault_set() để đẩy cảnh báo lên
// server (qua ie_ws_send_alert). Có latch theo code: chỉ gửi khi CHUYỂN trạng
// thái (lần đầu bật / khi clear) → không spam server dù gọi trong vòng lặp.
//
// Dùng cho LỖI TRẠNG THÁI (có lúc hết): set khi vào lỗi, clear khi thoát.
// Với LỖI SỰ KIỆN một lần (persist fail, init fail) chỉ cần gọi set().

#ifdef __cplusplus
extern "C" {
#endif

// ie_fault_set: báo lỗi code đang xảy ra. severity NULL → "warning". Chỉ thật
// sự gửi frame nếu code chưa active (chống lặp).
void ie_fault_set(const char *code, const char *severity, const char *message);

// ie_fault_clear: báo lỗi code đã hết. Chỉ gửi nếu code đang active.
void ie_fault_clear(const char *code);

// Gửi lại mọi cảnh báo còn active — ws_client gọi mỗi lần kết nối (cloud gom
// theo (device, code) nên gửi lặp không tạo bản ghi trùng).
void ie_fault_resend_active(void);

#ifdef __cplusplus
}
#endif
