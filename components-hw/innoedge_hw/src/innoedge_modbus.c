// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#include "innoedge_modbus.h"
#include <string.h>

uint16_t innoedge_modbus_crc16(const uint8_t *buffer, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)buffer[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

esp_err_t innoedge_modbus_build_read_registers(uint8_t slave_addr, uint8_t function_code,
                                              uint16_t start_reg, uint16_t reg_count,
                                              uint8_t *out_buf, size_t *out_len)
{
    if (!out_buf || !out_len || reg_count == 0 || reg_count > 125) {
        return ESP_ERR_INVALID_ARG;
    }
    out_buf[0] = slave_addr;
    out_buf[1] = function_code;
    out_buf[2] = (uint8_t)((start_reg >> 8) & 0xFF);
    out_buf[3] = (uint8_t)(start_reg & 0xFF);
    out_buf[4] = (uint8_t)((reg_count >> 8) & 0xFF);
    out_buf[5] = (uint8_t)(reg_count & 0xFF);

    uint16_t crc = innoedge_modbus_crc16(out_buf, 6);
    out_buf[6] = (uint8_t)(crc & 0xFF);         // Low byte first trong chuẩn Modbus
    out_buf[7] = (uint8_t)((crc >> 8) & 0xFF);  // High byte

    *out_len = 8;
    return ESP_OK;
}

esp_err_t innoedge_modbus_build_write_single_register(uint8_t slave_addr, uint16_t reg_addr,
                                                     uint16_t value,
                                                     uint8_t *out_buf, size_t *out_len)
{
    if (!out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    out_buf[0] = slave_addr;
    out_buf[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    out_buf[2] = (uint8_t)((reg_addr >> 8) & 0xFF);
    out_buf[3] = (uint8_t)(reg_addr & 0xFF);
    out_buf[4] = (uint8_t)((value >> 8) & 0xFF);
    out_buf[5] = (uint8_t)(value & 0xFF);

    uint16_t crc = innoedge_modbus_crc16(out_buf, 6);
    out_buf[6] = (uint8_t)(crc & 0xFF);
    out_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    *out_len = 8;
    return ESP_OK;
}

esp_err_t innoedge_modbus_parse_response(uint8_t slave_addr, uint8_t expected_fc,
                                        const uint8_t *rx_data, size_t rx_len,
                                        uint16_t *out_registers, size_t max_regs,
                                        size_t *out_reg_count)
{
    if (!rx_data || rx_len < 5 || !out_registers || !out_reg_count) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Kiểm tra CRC
    uint16_t received_crc = (uint16_t)rx_data[rx_len - 2] | ((uint16_t)rx_data[rx_len - 1] << 8);
    uint16_t calculated_crc = innoedge_modbus_crc16(rx_data, rx_len - 2);
    if (received_crc != calculated_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    // 2. Kiểm tra Địa chỉ Slave
    if (rx_data[0] != slave_addr) {
        return ESP_ERR_NOT_FOUND;
    }

    // 3. Kiểm tra Mã lỗi Exception từ Slave (FC | 0x80)
    if (rx_data[1] == (expected_fc | 0x80)) {
        return ESP_FAIL;
    }

    if (rx_data[1] != expected_fc) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 4. Trích xuất thanh ghi cho FC 0x03 / 0x04
    if (expected_fc == MODBUS_FC_READ_HOLDING_REGISTERS || expected_fc == MODBUS_FC_READ_INPUT_REGISTERS) {
        uint8_t byte_count = rx_data[2];
        if (rx_len != (size_t)(3 + byte_count + 2)) {
            return ESP_ERR_INVALID_SIZE;
        }

        size_t count = byte_count / 2;
        if (count > max_regs) {
            count = max_regs;
        }

        for (size_t i = 0; i < count; i++) {
            out_registers[i] = ((uint16_t)rx_data[3 + (i * 2)] << 8) | rx_data[3 + (i * 2) + 1];
        }
        *out_reg_count = count;
        return ESP_OK;
    }

    *out_reg_count = 0;
    return ESP_OK;
}
