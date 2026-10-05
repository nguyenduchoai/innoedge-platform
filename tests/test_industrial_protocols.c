// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#include "innoedge_mdb.h"
#include "innoedge_modbus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_mdb_events = 0;
static innoedge_mdb_event_type_t g_last_evt_type;
static uint16_t g_last_price = 0;
static uint16_t g_last_item = 0;

static void on_mdb_event(const innoedge_mdb_event_t *evt, void *user_ctx)
{
    (void)user_ctx;
    g_mdb_events++;
    g_last_evt_type = evt->type;
    g_last_price = evt->item_price_cents;
    g_last_item = evt->item_number;
}

static void test_mdb_state_machine(void)
{
    printf("  [MDB] Khởi tạo và reset VMC...\n");
    innoedge_mdb_config_t cfg = {
        .uart_port = 1,
        .tx_pin = 4,
        .rx_pin = 5,
        .currency_code_high = 0x17,
        .currency_code_low = 0x04,
        .scaling_factor = 100,
        .event_cb = on_mdb_event,
        .user_ctx = NULL
    };
    assert(innoedge_mdb_init(&cfg) == ESP_OK);
    assert(innoedge_mdb_get_state() == INNOEDGE_MDB_STATE_INACTIVE);

    uint8_t resp[32];
    size_t resp_len = 0;

    // 1. VMC gửi lệnh RESET (0x10, Mode bit = 1)
    bool ok = innoedge_mdb_process_byte(0x10, true, resp, &resp_len);
    assert(ok && resp_len == 1 && resp[0] == 0x00);
    assert(innoedge_mdb_get_state() == INNOEDGE_MDB_STATE_DISABLED);
    assert(g_last_evt_type == INNOEDGE_MDB_EVT_RESET);

    // 2. VMC gửi lệnh POLL -> Reader trả về JUST RESET (0x00) kèm checksum
    ok = innoedge_mdb_process_byte(0x12, true, resp, &resp_len);
    assert(ok && resp_len == 2 && resp[0] == 0x00 && resp[1] == 0x00);

    // 3. VMC gửi lệnh SETUP (0x11, Mode bit = 1) kèm data: 0x00, 0x01, 0x02, 0x03, 0x04, checksum
    assert(innoedge_mdb_process_byte(0x11, true, resp, &resp_len) == false); // đang nhận data
    innoedge_mdb_process_byte(0x00, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x01, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x02, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x03, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x04, false, resp, &resp_len);
    ok = innoedge_mdb_process_byte(0x1B, false, resp, &resp_len); // chk
    assert(ok && resp_len == 9 && resp[0] == 0x01); // Reader Config
    assert(resp[4] == 100); // scaling factor

    // 4. VMC gửi lệnh READER ENABLE (0x14 0x01 0x15)
    innoedge_mdb_process_byte(0x14, true, resp, &resp_len);
    innoedge_mdb_process_byte(0x01, false, resp, &resp_len);
    ok = innoedge_mdb_process_byte(0x15, false, resp, &resp_len);
    assert(ok && resp_len == 1 && resp[0] == 0x00);
    assert(innoedge_mdb_get_state() == INNOEDGE_MDB_STATE_ENABLED);
    assert(g_last_evt_type == INNOEDGE_MDB_EVT_READER_ENABLED);

    // 5. Cloud báo tiền về (quét QR thành công 25000 đ) -> Begin Session
    assert(innoedge_mdb_begin_session(250) == ESP_OK); // 250 units = 25.000 đ
    assert(innoedge_mdb_get_state() == INNOEDGE_MDB_STATE_SESSION_IDLE);

    // VMC POLL -> Reader trả về BEGIN SESSION kèm số dư 250
    ok = innoedge_mdb_process_byte(0x12, true, resp, &resp_len);
    assert(ok && resp_len == 6 && resp[0] == 0x03);
    uint16_t funds = ((uint16_t)resp[1] << 8) | resp[2];
    assert(funds == 250);

    // 6. Khách bấm nút chọn lon nước trên máy bán nước: VMC gửi VEND REQUEST (món giá 200, item 5)
    innoedge_mdb_process_byte(0x13, true, resp, &resp_len);
    innoedge_mdb_process_byte(0x00, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x00, false, resp, &resp_len);
    innoedge_mdb_process_byte(0xC8, false, resp, &resp_len); // 200 (20.000 đ)
    innoedge_mdb_process_byte(0x00, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x05, false, resp, &resp_len); // khay 5
    ok = innoedge_mdb_process_byte(0xE0, false, resp, &resp_len);
    assert(ok && resp_len == 1 && resp[0] == 0x00);
    assert(innoedge_mdb_get_state() == INNOEDGE_MDB_STATE_VEND);
    assert(g_last_evt_type == INNOEDGE_MDB_EVT_VEND_REQUEST);
    assert(g_last_price == 200);
    assert(g_last_item == 5);

    // 7. Reader chấp thuận nhả hàng
    assert(innoedge_mdb_approve_vend(200) == ESP_OK);
    ok = innoedge_mdb_process_byte(0x12, true, resp, &resp_len);
    assert(ok && resp_len == 4 && resp[0] == 0x05); // VEND APPROVED

    // 8. Cảm biến rơi của máy xác nhận hàng đã rơi thành công: VEND SUCCESS
    innoedge_mdb_process_byte(0x13, true, resp, &resp_len);
    innoedge_mdb_process_byte(0x02, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x00, false, resp, &resp_len);
    innoedge_mdb_process_byte(0x05, false, resp, &resp_len);
    ok = innoedge_mdb_process_byte(0x1A, false, resp, &resp_len);
    assert(ok && resp_len == 1 && resp[0] == 0x00);
    assert(g_last_evt_type == INNOEDGE_MDB_EVT_VEND_SUCCESS);
    assert(g_last_price == 200);

    printf("  [MDB] PASS: Máy bán nước MDB hoàn tất chu trình mua hàng!\n");
}

static void test_modbus_rtu(void)
{
    printf("  [Modbus RTU] Kiểm tra CRC16 và gói tin đọc thanh ghi...\n");

    // Test vector chuẩn: [0x01, 0x03, 0x00, 0x00, 0x00, 0x0A]
    uint8_t test_msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = innoedge_modbus_crc16(test_msg, sizeof(test_msg));
    assert(crc == 0xCDC5); // Low byte: 0xC5, High byte: 0xCD

    // Tạo gói tin đọc 2 thanh ghi từ Slave 1
    uint8_t tx_buf[16];
    size_t tx_len = 0;
    esp_err_t err = innoedge_modbus_build_read_registers(1, MODBUS_FC_READ_HOLDING_REGISTERS,
                                                        0x0010, 2, tx_buf, &tx_len);
    assert(err == ESP_OK && tx_len == 8);
    assert(tx_buf[0] == 1 && tx_buf[1] == 0x03 && tx_buf[2] == 0x00 && tx_buf[3] == 0x10);

    // Giả lập Slave trả lời: Slave 1, FC 0x03, 4 bytes dữ liệu: [0x08, 0x98, 0x00, 0x64] (2200 và 100)
    uint8_t slave_resp[9] = {1, 0x03, 4, 0x08, 0x98, 0x00, 0x64, 0, 0};
    uint16_t resp_crc = innoedge_modbus_crc16(slave_resp, 7);
    slave_resp[7] = (uint8_t)(resp_crc & 0xFF);
    slave_resp[8] = (uint8_t)((resp_crc >> 8) & 0xFF);

    uint16_t regs[8];
    size_t reg_count = 0;
    err = innoedge_modbus_parse_response(1, MODBUS_FC_READ_HOLDING_REGISTERS,
                                        slave_resp, sizeof(slave_resp),
                                        regs, 8, &reg_count);
    assert(err == ESP_OK);
    assert(reg_count == 2);
    assert(regs[0] == 2200); // 220.0 V
    assert(regs[1] == 100);  // 10.0 A

    // Kiểm tra phát hiện CRC lỗi
    slave_resp[7] ^= 0xFF;
    err = innoedge_modbus_parse_response(1, MODBUS_FC_READ_HOLDING_REGISTERS,
                                        slave_resp, sizeof(slave_resp),
                                        regs, 8, &reg_count);
    assert(err == ESP_ERR_INVALID_CRC);

    printf("  [Modbus RTU] PASS: Đọc thanh ghi và kiểm tra CRC16 hoàn hảo!\n");
}

int main(void)
{
    test_mdb_state_machine();
    test_modbus_rtu();
    printf("PASS — Toàn bộ kiểm tra Giao thức Công nghiệp (MDB & Modbus RTU)\n");
    return 0;
}
