// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t seq;
    char json[192];
} ie_payment_event_t;

esp_err_t ie_payment_queue_init(void);
// ie_payment_queue_append: thêm 1 sự kiện thanh toán vào hàng đợi bền (NVS).
// out_dropped (có thể NULL): set true nếu hàng đợi ĐẦY và phải ghi đè sự kiện
// CŨ NHẤT (mất tiền đã thu) — caller PHẢI phát cảnh báo critical, không để âm thầm.
esp_err_t ie_payment_queue_append(uint64_t seq, const char *json, bool *out_dropped);
esp_err_t ie_payment_queue_peek(ie_payment_event_t *event);
esp_err_t ie_payment_queue_ack(uint64_t seq);
uint32_t ie_payment_queue_count(void);

#ifdef __cplusplus
}
#endif
