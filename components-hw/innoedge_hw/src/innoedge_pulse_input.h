// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INNOEDGE_PULSE_COIN = 0,
    INNOEDGE_PULSE_BILL = 1,
} innoedge_pulse_kind_t;


typedef void (*innoedge_pulse_input_cb_t)(innoedge_pulse_kind_t kind, uint32_t pulses,
                                          int64_t amount_vnd, void *ctx);

// Nguồn đọc "coin active" ngoài GPIO (vd IO-expander trên board có màn hình).
typedef esp_err_t (*innoedge_pulse_expander_read_fn)(bool *active);

void innoedge_pulse_input_set_expander_reader(innoedge_pulse_expander_read_fn fn);

esp_err_t innoedge_pulse_input_init(innoedge_pulse_input_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
