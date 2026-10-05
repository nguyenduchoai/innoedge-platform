// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_net_failover.h"

#define PING_FAIL_THRESHOLD     3
#define RECOVERY_STABLE_SECONDS 30

static innoedge_net_interface_t s_active_iface = INNOEDGE_NET_PRIMARY;
static gtek_failover_state_t s_state = GTEK_FAILOVER_STATE_PRIMARY_OK;

static bool s_primary_link_up = true;
static bool s_secondary_link_up = true;
static int  s_consecutive_fails = 0;
static int  s_recovery_seconds = 0;

esp_err_t gtek_net_failover_init(void)
{
    s_active_iface = INNOEDGE_NET_PRIMARY;
    s_state = GTEK_FAILOVER_STATE_PRIMARY_OK;
    s_primary_link_up = true;
    s_secondary_link_up = true;
    s_consecutive_fails = 0;
    s_recovery_seconds = 0;
    return ESP_OK;
}

innoedge_net_interface_t gtek_net_failover_get_active(void)
{
    return s_active_iface;
}

gtek_failover_state_t gtek_net_failover_get_state(void)
{
    return s_state;
}

void gtek_net_failover_report_link(innoedge_net_interface_t iface, bool is_up)
{
    if (iface == INNOEDGE_NET_PRIMARY) {
        s_primary_link_up = is_up;
        if (!is_up && s_active_iface == INNOEDGE_NET_PRIMARY) {
            // Rớt cáp/WiFi vật lý → chuyển ngay sang secondary
            s_active_iface = INNOEDGE_NET_SECONDARY;
            s_state = GTEK_FAILOVER_STATE_SECONDARY_ACTIVE;
            s_consecutive_fails = 0;
            s_recovery_seconds = 0;
        } else if (is_up && s_active_iface == INNOEDGE_NET_SECONDARY) {
            // Có lại WiFi vật lý → vào trạng thái chờ ổn định
            s_state = GTEK_FAILOVER_STATE_RECOVERING;
            s_recovery_seconds = 0;
        }
    } else {
        s_secondary_link_up = is_up;
    }
}

void gtek_net_failover_report_ping_success(void)
{
    s_consecutive_fails = 0;
    if (s_active_iface == INNOEDGE_NET_PRIMARY) {
        s_state = GTEK_FAILOVER_STATE_PRIMARY_OK;
    }
}

void gtek_net_failover_report_ping_lost(void)
{
    if (s_active_iface == INNOEDGE_NET_PRIMARY) {
        s_consecutive_fails++;
        if (s_consecutive_fails >= PING_FAIL_THRESHOLD) {
            // Ping mất quá ngưỡng → failover sang secondary
            s_active_iface = INNOEDGE_NET_SECONDARY;
            s_state = GTEK_FAILOVER_STATE_SECONDARY_ACTIVE;
            s_consecutive_fails = 0;
            s_recovery_seconds = 0;
        } else {
            s_state = GTEK_FAILOVER_STATE_DEGRADED;
        }
    }
}

bool gtek_net_failover_tick(void)
{
    innoedge_net_interface_t prev_iface = s_active_iface;

    if (s_active_iface == INNOEDGE_NET_SECONDARY) {
        if (s_primary_link_up) {
            s_recovery_seconds++;
            if (s_recovery_seconds >= RECOVERY_STABLE_SECONDS) {
                // Đủ thời gian ổn định → chuyển ngược về primary
                s_active_iface = INNOEDGE_NET_PRIMARY;
                s_state = GTEK_FAILOVER_STATE_PRIMARY_OK;
                s_recovery_seconds = 0;
                s_consecutive_fails = 0;
            }
        } else {
            s_recovery_seconds = 0;
        }
    }

    return (s_active_iface != prev_iface);
}
