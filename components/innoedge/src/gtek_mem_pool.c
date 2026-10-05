// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_mem_pool.h"

#include <string.h>

#define POOL_SMALL_BLOCK_SIZE  128
#define POOL_SMALL_COUNT       16

#define POOL_MED_BLOCK_SIZE    512
#define POOL_MED_COUNT         8

#define POOL_LARGE_BLOCK_SIZE  2048
#define POOL_LARGE_COUNT       4

// Vùng nhớ tĩnh hoàn toàn — không cấp phát động, không phân mảnh heap
static uint8_t s_pool_small[POOL_SMALL_COUNT][POOL_SMALL_BLOCK_SIZE];
static uint16_t s_pool_small_mask = 0; // bitmask theo dõi khối đang dùng

static uint8_t s_pool_med[POOL_MED_COUNT][POOL_MED_BLOCK_SIZE];
static uint8_t s_pool_med_mask = 0;

static uint8_t s_pool_large[POOL_LARGE_COUNT][POOL_LARGE_BLOCK_SIZE];
static uint8_t s_pool_large_mask = 0;

esp_err_t gtek_mem_pool_init(void)
{
    s_pool_small_mask = 0;
    s_pool_med_mask = 0;
    s_pool_large_mask = 0;
    return ESP_OK;
}

void *gtek_mem_pool_alloc(size_t size)
{
    if (size == 0) return NULL;

    if (size <= POOL_SMALL_BLOCK_SIZE) {
        for (int i = 0; i < POOL_SMALL_COUNT; i++) {
            if (!(s_pool_small_mask & (1 << i))) {
                s_pool_small_mask |= (1 << i);
                return s_pool_small[i];
            }
        }
    }

    if (size <= POOL_MED_BLOCK_SIZE) {
        for (int i = 0; i < POOL_MED_COUNT; i++) {
            if (!(s_pool_med_mask & (1 << i))) {
                s_pool_med_mask |= (1 << i);
                return s_pool_med[i];
            }
        }
    }

    if (size <= POOL_LARGE_BLOCK_SIZE) {
        for (int i = 0; i < POOL_LARGE_COUNT; i++) {
            if (!(s_pool_large_mask & (1 << i))) {
                s_pool_large_mask |= (1 << i);
                return s_pool_large[i];
            }
        }
    }

    return NULL; // Hết bộ nhớ static
}

void gtek_mem_pool_free(void *ptr)
{
    if (!ptr) return;

    // Kiểm tra pool small
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t start_s = (uintptr_t)s_pool_small;
    uintptr_t end_s = start_s + sizeof(s_pool_small);
    if (p >= start_s && p < end_s) {
        size_t idx = (p - start_s) / POOL_SMALL_BLOCK_SIZE;
        if (idx < POOL_SMALL_COUNT) {
            s_pool_small_mask &= ~(1 << idx);
        }
        return;
    }

    // Kiểm tra pool med
    uintptr_t start_m = (uintptr_t)s_pool_med;
    uintptr_t end_m = start_m + sizeof(s_pool_med);
    if (p >= start_m && p < end_m) {
        size_t idx = (p - start_m) / POOL_MED_BLOCK_SIZE;
        if (idx < POOL_MED_COUNT) {
            s_pool_med_mask &= ~(1 << idx);
        }
        return;
    }

    // Kiểm tra pool large
    uintptr_t start_l = (uintptr_t)s_pool_large;
    uintptr_t end_l = start_l + sizeof(s_pool_large);
    if (p >= start_l && p < end_l) {
        size_t idx = (p - start_l) / POOL_LARGE_BLOCK_SIZE;
        if (idx < POOL_LARGE_COUNT) {
            s_pool_large_mask &= ~(1 << idx);
        }
        return;
    }
}

// ── Ring Buffer ─────────────────────────────────────────────────────────────

static bool is_power_of_two(size_t n)
{
    return (n != 0) && ((n & (n - 1)) == 0);
}

esp_err_t gtek_ring_buf_init(gtek_ring_buf_t *rb, uint8_t *storage, size_t size)
{
    if (!rb || !storage || !is_power_of_two(size)) {
        return ESP_ERR_INVALID_ARG;
    }
    rb->storage = storage;
    rb->size_mask = size - 1;
    rb->write_idx = 0;
    rb->read_idx = 0;
    return ESP_OK;
}

size_t gtek_ring_buf_available(const gtek_ring_buf_t *rb)
{
    if (!rb) return 0;
    return rb->write_idx - rb->read_idx;
}

size_t gtek_ring_buf_free_space(const gtek_ring_buf_t *rb)
{
    if (!rb) return 0;
    size_t capacity = rb->size_mask + 1;
    return capacity - (rb->write_idx - rb->read_idx);
}

void gtek_ring_buf_reset(gtek_ring_buf_t *rb)
{
    if (!rb) return;
    rb->write_idx = 0;
    rb->read_idx = 0;
}

size_t gtek_ring_buf_write(gtek_ring_buf_t *rb, const uint8_t *data, size_t len)
{
    if (!rb || !data || len == 0) return 0;

    size_t free_sp = gtek_ring_buf_free_space(rb);
    size_t to_write = (len > free_sp) ? free_sp : len;

    for (size_t i = 0; i < to_write; i++) {
        rb->storage[(rb->write_idx + i) & rb->size_mask] = data[i];
    }
    rb->write_idx += to_write;
    return to_write;
}

size_t gtek_ring_buf_read(gtek_ring_buf_t *rb, uint8_t *out, size_t max_len)
{
    if (!rb || !out || max_len == 0) return 0;

    size_t avail = gtek_ring_buf_available(rb);
    size_t to_read = (max_len > avail) ? avail : max_len;

    for (size_t i = 0; i < to_read; i++) {
        out[i] = rb->storage[(rb->read_idx + i) & rb->size_mask];
    }
    rb->read_idx += to_read;
    return to_read;
}
