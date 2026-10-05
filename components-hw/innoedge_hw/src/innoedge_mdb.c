// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#include "innoedge_mdb.h"
#include <string.h>
#include <stdio.h>

#define MDB_ADDR_CASHLESS_1        0x10
#define MDB_CMD_RESET              0x10
#define MDB_CMD_SETUP              0x11
#define MDB_CMD_POLL               0x12
#define MDB_CMD_VEND               0x13
#define MDB_CMD_READER             0x14
#define MDB_CMD_EXPANSION          0x17

// Mã phản hồi MDB Peripheral
#define MDB_RESP_ACK               0x00
#define MDB_RESP_JUST_RESET        0x00
#define MDB_RESP_READER_CONFIG     0x01
#define MDB_RESP_BEGIN_SESSION     0x03
#define MDB_RESP_SESSION_CANCEL    0x04
#define MDB_RESP_VEND_APPROVED     0x05
#define MDB_RESP_VEND_DENIED       0x06
#define MDB_RESP_END_SESSION       0x07

typedef enum {
    MDB_PENDING_NONE = 0,
    MDB_PENDING_JUST_RESET,
    MDB_PENDING_BEGIN_SESSION,
    MDB_PENDING_VEND_APPROVED,
    MDB_PENDING_VEND_DENIED,
    MDB_PENDING_END_SESSION
} mdb_pending_resp_t;

static innoedge_mdb_config_t s_cfg;
static innoedge_mdb_state_t s_state = INNOEDGE_MDB_STATE_INACTIVE;
static mdb_pending_resp_t s_pending = MDB_PENDING_JUST_RESET;

static uint16_t s_available_funds = 0;
static uint16_t s_approved_amount = 0;

static uint8_t s_rx_buf[32];
static size_t s_rx_idx = 0;
static bool s_cmd_in_progress = false;

static uint8_t calculate_checksum(const uint8_t *data, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

esp_err_t innoedge_mdb_init(const innoedge_mdb_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memcpy(&s_cfg, cfg, sizeof(innoedge_mdb_config_t));
    s_state = INNOEDGE_MDB_STATE_INACTIVE;
    s_pending = MDB_PENDING_JUST_RESET;
    s_rx_idx = 0;
    s_cmd_in_progress = false;
    return ESP_OK;
}

innoedge_mdb_state_t innoedge_mdb_get_state(void)
{
    return s_state;
}

esp_err_t innoedge_mdb_begin_session(uint16_t available_funds)
{
    if (s_state != INNOEDGE_MDB_STATE_ENABLED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_available_funds = available_funds;
    s_pending = MDB_PENDING_BEGIN_SESSION;
    s_state = INNOEDGE_MDB_STATE_SESSION_IDLE;
    return ESP_OK;
}

esp_err_t innoedge_mdb_approve_vend(uint16_t approved_amount)
{
    if (s_state != INNOEDGE_MDB_STATE_VEND) {
        return ESP_ERR_INVALID_STATE;
    }
    s_approved_amount = approved_amount;
    s_pending = MDB_PENDING_VEND_APPROVED;
    return ESP_OK;
}

esp_err_t innoedge_mdb_deny_vend(void)
{
    if (s_state != INNOEDGE_MDB_STATE_VEND) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pending = MDB_PENDING_VEND_DENIED;
    return ESP_OK;
}

esp_err_t innoedge_mdb_end_session(void)
{
    s_pending = MDB_PENDING_END_SESSION;
    s_state = INNOEDGE_MDB_STATE_ENABLED;
    return ESP_OK;
}

static void notify_event(innoedge_mdb_event_type_t type, uint16_t price, uint16_t item)
{
    if (s_cfg.event_cb) {
        innoedge_mdb_event_t evt = {
            .type = type,
            .item_price_cents = price,
            .item_number = item
        };
        s_cfg.event_cb(&evt, s_cfg.user_ctx);
    }
}

bool innoedge_mdb_process_byte(uint8_t byte, bool is_mode_bit_set, uint8_t *out_resp, size_t *out_len)
{
    if (!out_resp || !out_len) return false;
    *out_len = 0;

    // Trong giao thức MDB: Byte có Mode bit = 1 là byte Địa chỉ bắt đầu frame lệnh
    if (is_mode_bit_set) {
        uint8_t addr = byte & 0xF8;
        if (addr == MDB_ADDR_CASHLESS_1) {
            s_rx_buf[0] = byte;
            s_rx_idx = 1;
            s_cmd_in_progress = true;

            // Lệnh RESET (0x10) hoặc POLL (0x12) không có thêm data byte
            if (byte == MDB_CMD_RESET) {
                s_state = INNOEDGE_MDB_STATE_DISABLED;
                s_pending = MDB_PENDING_JUST_RESET;
                notify_event(INNOEDGE_MDB_EVT_RESET, 0, 0);
                out_resp[0] = MDB_RESP_ACK;
                *out_len = 1;
                s_cmd_in_progress = false;
                return true;
            } else if (byte == MDB_CMD_POLL) {
                // Phản hồi POLL từ VMC
                switch (s_pending) {
                    case MDB_PENDING_JUST_RESET:
                        out_resp[0] = MDB_RESP_JUST_RESET;
                        out_resp[1] = calculate_checksum(out_resp, 1);
                        *out_len = 2;
                        s_pending = MDB_PENDING_NONE;
                        break;
                    case MDB_PENDING_BEGIN_SESSION:
                        out_resp[0] = MDB_RESP_BEGIN_SESSION;
                        out_resp[1] = (uint8_t)((s_available_funds >> 8) & 0xFF);
                        out_resp[2] = (uint8_t)(s_available_funds & 0xFF);
                        out_resp[3] = 0xFF; // Payment media ID
                        out_resp[4] = 0xFF;
                        out_resp[5] = calculate_checksum(out_resp, 5);
                        *out_len = 6;
                        s_pending = MDB_PENDING_NONE;
                        break;
                    case MDB_PENDING_VEND_APPROVED:
                        out_resp[0] = MDB_RESP_VEND_APPROVED;
                        out_resp[1] = (uint8_t)((s_approved_amount >> 8) & 0xFF);
                        out_resp[2] = (uint8_t)(s_approved_amount & 0xFF);
                        out_resp[3] = calculate_checksum(out_resp, 3);
                        *out_len = 4;
                        s_pending = MDB_PENDING_NONE;
                        break;
                    case MDB_PENDING_VEND_DENIED:
                        out_resp[0] = MDB_RESP_VEND_DENIED;
                        out_resp[1] = calculate_checksum(out_resp, 1);
                        *out_len = 2;
                        s_pending = MDB_PENDING_NONE;
                        s_state = INNOEDGE_MDB_STATE_SESSION_IDLE;
                        break;
                    case MDB_PENDING_END_SESSION:
                        out_resp[0] = MDB_RESP_END_SESSION;
                        out_resp[1] = calculate_checksum(out_resp, 1);
                        *out_len = 2;
                        s_pending = MDB_PENDING_NONE;
                        s_state = INNOEDGE_MDB_STATE_ENABLED;
                        break;
                    default:
                        out_resp[0] = MDB_RESP_ACK; // Im lặng nếu không có sự kiện
                        *out_len = 1;
                        break;
                }
                s_cmd_in_progress = false;
                return true;
            }
            return false;
        } else {
            // Lệnh gửi cho thiết bị MDB khác (Coin Changer 0x08, Bill Validator 0x30...) -> bỏ qua
            s_cmd_in_progress = false;
            return false;
        }
    }

    if (!s_cmd_in_progress) return false;

    // Gom data byte vào buffer
    if (s_rx_idx < sizeof(s_rx_buf)) {
        s_rx_buf[s_rx_idx++] = byte;
    }

    uint8_t main_cmd = s_rx_buf[0];

    // Lệnh SETUP (0x11): sub-cmd 0x00 + 4 data bytes + 1 chk = 7 bytes
    if (main_cmd == MDB_CMD_SETUP && s_rx_idx >= 7) {
        // Trả lời cấu hình Reader Config
        out_resp[0] = MDB_RESP_READER_CONFIG;
        out_resp[1] = 0x02; // Reader Feature Level 2
        out_resp[2] = s_cfg.currency_code_high ? s_cfg.currency_code_high : 0x17; // ISO Currency Code
        out_resp[3] = s_cfg.currency_code_low ? s_cfg.currency_code_low : 0x04;
        out_resp[4] = s_cfg.scaling_factor ? s_cfg.scaling_factor : 100;
        out_resp[5] = 0x02; // Decimal places: 2
        out_resp[6] = 0x1E; // Max response time: 30s
        out_resp[7] = 0x03; // Options: Restore power, Multivend
        out_resp[8] = calculate_checksum(out_resp, 8);
        *out_len = 9;
        s_cmd_in_progress = false;
        s_state = INNOEDGE_MDB_STATE_DISABLED;
        return true;
    }

    // Lệnh READER (0x14): sub-cmd + 1 chk = 3 bytes
    if (main_cmd == MDB_CMD_READER && s_rx_idx >= 3) {
        uint8_t sub = s_rx_buf[1];
        if (sub == 0x00) { // Disable
            s_state = INNOEDGE_MDB_STATE_DISABLED;
            notify_event(INNOEDGE_MDB_EVT_READER_DISABLED, 0, 0);
        } else if (sub == 0x01) { // Enable
            s_state = INNOEDGE_MDB_STATE_ENABLED;
            notify_event(INNOEDGE_MDB_EVT_READER_ENABLED, 0, 0);
        } else if (sub == 0x02) { // Cancel
            s_state = INNOEDGE_MDB_STATE_DISABLED;
            notify_event(INNOEDGE_MDB_EVT_SESSION_CANCELLED, 0, 0);
        }
        out_resp[0] = MDB_RESP_ACK;
        *out_len = 1;
        s_cmd_in_progress = false;
        return true;
    }

    // Lệnh VEND (0x13):
    if (main_cmd == MDB_CMD_VEND) {
        uint8_t sub = s_rx_buf[1];
        // 0x13 0x00: VEND REQUEST (sub + 2 byte price + 2 byte item + 1 chk = 7 bytes)
        if (sub == 0x00 && s_rx_idx >= 7) {
            uint16_t price = ((uint16_t)s_rx_buf[2] << 8) | s_rx_buf[3];
            uint16_t item = ((uint16_t)s_rx_buf[4] << 8) | s_rx_buf[5];
            s_state = INNOEDGE_MDB_STATE_VEND;
            notify_event(INNOEDGE_MDB_EVT_VEND_REQUEST, price, item);
            out_resp[0] = MDB_RESP_ACK;
            *out_len = 1;
            s_cmd_in_progress = false;
            return true;
        }
        // 0x13 0x02: VEND SUCCESS (sub + 2 byte item + 1 chk = 5 bytes)
        if (sub == 0x02 && s_rx_idx >= 5) {
            uint16_t item = ((uint16_t)s_rx_buf[2] << 8) | s_rx_buf[3];
            s_state = INNOEDGE_MDB_STATE_ENABLED;
            notify_event(INNOEDGE_MDB_EVT_VEND_SUCCESS, s_approved_amount, item);
            out_resp[0] = MDB_RESP_ACK;
            *out_len = 1;
            s_cmd_in_progress = false;
            return true;
        }
        // 0x13 0x03: VEND FAILURE (sub + 1 chk = 3 bytes)
        if (sub == 0x03 && s_rx_idx >= 3) {
            s_state = INNOEDGE_MDB_STATE_SESSION_IDLE;
            notify_event(INNOEDGE_MDB_EVT_VEND_FAILED, 0, 0);
            out_resp[0] = MDB_RESP_ACK;
            *out_len = 1;
            s_cmd_in_progress = false;
            return true;
        }
    }

    return false;
}
