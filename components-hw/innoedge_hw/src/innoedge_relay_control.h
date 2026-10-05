// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Relay đa kênh ───────────────────────────────────────────────────────────
// Mỗi kênh map tới 1 GPIO cấu hình qua Kconfig (mặc định -1 = vô hiệu hoá).
typedef enum {
    INNOEDGE_RELAY_WATER = 0,  // xịt nước
    INNOEDGE_RELAY_FOAM,       // phun bọt
    INNOEDGE_RELAY_AIR,        // nén khí
    INNOEDGE_RELAY_VACUUM,     // hút bụi
    INNOEDGE_RELAY_DISPENSE,   // relay nhả tiền/credit
    INNOEDGE_RELAY_COUNT
} innoedge_relay_channel_t;

// Compatibility aliases
#define GTEK_RELAY_WATER INNOEDGE_RELAY_WATER
#define GTEK_RELAY_FOAM INNOEDGE_RELAY_FOAM
#define GTEK_RELAY_AIR INNOEDGE_RELAY_AIR
#define GTEK_RELAY_VACUUM INNOEDGE_RELAY_VACUUM
#define GTEK_RELAY_DISPENSE INNOEDGE_RELAY_DISPENSE
#define GTEK_RELAY_COUNT INNOEDGE_RELAY_COUNT
typedef innoedge_relay_channel_t gtek_relay_channel_t;

// Khởi tạo tất cả kênh có GPIO hợp lệ về mức 0 (OFF).
esp_err_t innoedge_relay_control_init(void);
#define gtek_relay_control_init innoedge_relay_control_init

// Bật/tắt một kênh.
esp_err_t innoedge_relay_set(innoedge_relay_channel_t channel, bool on);
#define gtek_relay_set innoedge_relay_set

// Bật DUY NHẤT một kênh, tắt mọi kênh "rửa" khác (WATER/FOAM/AIR/VACUUM).
esp_err_t innoedge_relay_set_exclusive(innoedge_relay_channel_t channel);
#define gtek_relay_set_exclusive innoedge_relay_set_exclusive

// Tắt toàn bộ kênh rửa (WATER/FOAM/AIR/VACUUM).
esp_err_t innoedge_relay_all_wash_off(void);
#define gtek_relay_all_wash_off innoedge_relay_all_wash_off

// Tên kênh để log/UI ("water"/"foam"/"air"/"vacuum"/"dispense").
const char *innoedge_relay_channel_name(innoedge_relay_channel_t channel);
#define gtek_relay_channel_name innoedge_relay_channel_name

// Relay nhả tiền dạng xung.
esp_err_t innoedge_relay_control_pulse(uint32_t pulse_ms);
#define gtek_relay_control_pulse innoedge_relay_control_pulse

#ifdef __cplusplus
}
#endif
