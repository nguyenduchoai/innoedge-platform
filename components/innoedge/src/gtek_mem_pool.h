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

// Bộ đệm tròn luân chuyển (Ring Buffer) có kích thước lũy thừa của 2
typedef struct {
    uint8_t *storage;
    size_t   size_mask; // size - 1 (với size là 2^N)
    size_t   write_idx;
    size_t   read_idx;
} gtek_ring_buf_t;

// Khởi tạo vùng nhớ tĩnh (Memory Pools)
esp_err_t gtek_mem_pool_init(void);

// Cấp phát một khối nhớ từ static pool theo kích thước (128B, 512B, 2048B).
// Trả về NULL nếu pool tương ứng đã hết khối trống (bảo vệ heap không bị phân mảnh).
void *gtek_mem_pool_alloc(size_t size);

// Thu hồi khối nhớ về static pool
void gtek_mem_pool_free(void *ptr);

// Khởi tạo ring buffer (size bắt buộc là 2^N, vd 512, 1024, 2048, 4096)
esp_err_t gtek_ring_buf_init(gtek_ring_buf_t *rb, uint8_t *storage, size_t size);

// Ghi dữ liệu vào ring buffer. Trả về số byte thực tế đã ghi.
size_t gtek_ring_buf_write(gtek_ring_buf_t *rb, const uint8_t *data, size_t len);

// Đọc dữ liệu ra khỏi ring buffer. Trả về số byte thực tế đã đọc.
size_t gtek_ring_buf_read(gtek_ring_buf_t *rb, uint8_t *out, size_t max_len);

// Lấy dung lượng dữ liệu đang có sẵn trong ring buffer (byte)
size_t gtek_ring_buf_available(const gtek_ring_buf_t *rb);

// Lấy dung lượng còn trống trong ring buffer (byte)
size_t gtek_ring_buf_free_space(const gtek_ring_buf_t *rb);

// Đặt lại (xả sạch) ring buffer
void gtek_ring_buf_reset(gtek_ring_buf_t *rb);

#ifdef __cplusplus
}
#endif
