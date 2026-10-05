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

typedef enum {
    INNOEDGE_NET_PRIMARY = 0,    // WiFi / Ethernet
    INNOEDGE_NET_SECONDARY = 1,  // 4G LTE / Cellular
} innoedge_net_interface_t;

typedef enum {
    GTEK_FAILOVER_STATE_PRIMARY_OK = 0,
    GTEK_FAILOVER_STATE_DEGRADED,
    GTEK_FAILOVER_STATE_SECONDARY_ACTIVE,
    GTEK_FAILOVER_STATE_RECOVERING,
} gtek_failover_state_t;

// Khởi tạo failover engine
esp_err_t gtek_net_failover_init(void);

// Lấy giao diện mạng hiện đang hoạt động
innoedge_net_interface_t gtek_net_failover_get_active(void);

// Cập nhật trạng thái kết nối vật lý của từng interface
void gtek_net_failover_report_link(innoedge_net_interface_t iface, bool is_up);

// Báo hiệu ping/heartbeat gửi thành công tới cloud
void gtek_net_failover_report_ping_success(void);

// Báo hiệu ping/heartbeat bị timeout/mất
void gtek_net_failover_report_ping_lost(void);

// Nhịp kiểm tra định kỳ (gọi mỗi 1 giây). Trả về true nếu vừa có sự kiện đổi interface.
bool gtek_net_failover_tick(void);

// Lấy trạng thái máy trạng thái nội bộ
gtek_failover_state_t gtek_net_failover_get_state(void);

#ifdef __cplusplus
}
#endif
