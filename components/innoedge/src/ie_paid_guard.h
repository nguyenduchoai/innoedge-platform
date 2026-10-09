// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once
//
// Chống giao hàng hai lần cho một khoản QR đã trả (`payment_paid`).
//
// Cloud gửi lại `payment_paid` tới khi nhận `paid_ack`; ack rơi lúc rớt mạng là
// frame tới lần nữa. Không chặn thì on_paid chạy hai lần = nhả hàng hai lần.
// Một watermark intentId cũng không đủ: hai khách trả lệch thứ tự (88 trước 87)
// thì 87 bị bỏ — khách trả tiền mà không có hàng. Nên dùng lại nhật ký từng-id
// của command bus (ie_command_journal.h), blob NVS riêng "paid_journal".

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IE_PAID_DELIVER = 0, // lần đầu: ack rồi gọi on_paid, xong gọi ie_paid_guard_done()
    IE_PAID_DUPLICATE,   // đã giao xong: CHỈ ack, không giao lại
    IE_PAID_UNCERTAIN,   // bị ngắt giữa chừng hoặc quá cũ (rơi khỏi cửa sổ): ack, KHÔNG
                         // giao, báo critical để đối soát — khách có thể đã trả mà chưa nhận
    IE_PAID_REFUSE,      // id sai hoặc không ghi được NVS: KHÔNG ack để cloud gửi lại
} ie_paid_decision_t;

// Nạp nhật ký; còn mục "đang giao" từ lần trước = mất điện giữa on_paid → alert.
void ie_paid_guard_init(void);

ie_paid_decision_t ie_paid_guard_begin(int64_t intent_id, size_t *slot);

// on_paid đã chạy xong. Lỗi ghi NVS ở đây chỉ làm mục kẹt "đang giao" (sau reboot
// sẽ báo đối soát) — không bao giờ dẫn tới giao lại.
void ie_paid_guard_done(size_t slot);

#ifdef __cplusplus
}
#endif
