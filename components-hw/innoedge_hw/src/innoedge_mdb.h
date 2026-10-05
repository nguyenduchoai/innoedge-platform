// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Trạng thái Cashless Device theo đặc tả MDB/ICP 4.3 (NAMA)
 */
typedef enum {
    INNOEDGE_MDB_STATE_INACTIVE = 0,
    INNOEDGE_MDB_STATE_DISABLED,
    INNOEDGE_MDB_STATE_ENABLED,
    INNOEDGE_MDB_STATE_SESSION_IDLE,
    INNOEDGE_MDB_STATE_VEND,
    INNOEDGE_MDB_STATE_REVALUE
} innoedge_mdb_state_t;

/**
 * @brief Sự kiện MDB từ VMC (Vending Machine Controller)
 */
typedef enum {
    INNOEDGE_MDB_EVT_RESET = 0,
    INNOEDGE_MDB_EVT_READER_ENABLED,
    INNOEDGE_MDB_EVT_READER_DISABLED,
    INNOEDGE_MDB_EVT_VEND_REQUEST,    // Khách chọn món trên phím máy bán nước
    INNOEDGE_MDB_EVT_VEND_SUCCESS,    // Cảm biến máy xác nhận hàng đã rơi thành công
    INNOEDGE_MDB_EVT_VEND_FAILED,     // Kẹt hàng, máy hủy lệnh
    INNOEDGE_MDB_EVT_SESSION_CANCELLED
} innoedge_mdb_event_type_t;

typedef struct {
    innoedge_mdb_event_type_t type;
    uint16_t item_price_cents;        // Giá món do VMC truyền xuống
    uint16_t item_number;             // Khay hàng / Mã sản phẩm (1-999)
} innoedge_mdb_event_t;

typedef void (*innoedge_mdb_event_cb_t)(const innoedge_mdb_event_t *evt, void *user_ctx);

typedef struct {
    int uart_port;                    // Ví dụ: UART_NUM_1 hoặc UART_NUM_2
    int tx_pin;                       // Chân TX qua mạch cách ly Opto MDB (Master/Slave)
    int rx_pin;                       // Chân RX qua mạch cách ly Opto MDB
    uint8_t currency_code_high;       // Mã tiền tệ ISO (ví dụ: 0x17, 0x04 cho VND hoặc USD)
    uint8_t currency_code_low;
    uint8_t scaling_factor;           // Hệ số nhân đơn vị (ví dụ 100 cho xu/cent)
    innoedge_mdb_event_cb_t event_cb; // Callback nhận sự kiện từ máy bán nước
    void *user_ctx;
} innoedge_mdb_config_t;

/**
 * @brief Khởi tạo driver MDB Cashless Peripheral
 */
esp_err_t innoedge_mdb_init(const innoedge_mdb_config_t *cfg);

/**
 * @brief Lấy trạng thái hiện tại của thiết bị MDB
 */
innoedge_mdb_state_t innoedge_mdb_get_state(void);

/**
 * @brief Bắt đầu phiên mua hàng từ Cloud (ví dụ khi khách quét QR thành công)
 * @param available_funds Số tiền cấp phép mua hàng (tính theo đơn vị scaling factor)
 */
esp_err_t innoedge_mdb_begin_session(uint16_t available_funds);

/**
 * @brief Cho phép nhả hàng (Chấp thuận yêu cầu Vend Request từ VMC)
 * @param approved_amount Số tiền thực tế thanh toán cho món hàng
 */
esp_err_t innoedge_mdb_approve_vend(uint16_t approved_amount);

/**
 * @brief Từ chối yêu cầu nhả hàng (ví dụ: số dư không đủ hoặc tài khoản bị khóa)
 */
esp_err_t innoedge_mdb_deny_vend(void);

/**
 * @brief Kết thúc phiên làm việc với VMC
 */
esp_err_t innoedge_mdb_end_session(void);

/**
 * @brief Xử lý byte thô nhận được từ bus MDB (dùng cho cả UART thật và test suite)
 * @param byte Dữ liệu 8-bit
 * @param is_mode_bit_set Cờ Mode bit (bit thứ 9 trong giao thức MDB)
 * @param out_resp Con trỏ mảng byte trả lời lại VMC
 * @param out_len Độ dài mảng trả lời
 */
bool innoedge_mdb_process_byte(uint8_t byte, bool is_mode_bit_set, uint8_t *out_resp, size_t *out_len);

#ifdef __cplusplus
}
#endif
