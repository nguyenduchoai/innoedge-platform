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

#define MODBUS_FC_READ_COILS               0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS     0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS   0x03
#define MODBUS_FC_READ_INPUT_REGISTERS     0x04
#define MODBUS_FC_WRITE_SINGLE_COIL        0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER    0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

/**
 * @brief Tính toán mã kiểm tra CRC-16 Modbus (đa thức 0xA001)
 */
uint16_t innoedge_modbus_crc16(const uint8_t *buffer, size_t length);

/**
 * @brief Tạo gói tin Modbus RTU Đọc Thanh Ghi (Read Holding/Input Registers - 0x03 / 0x04)
 * @param slave_addr Địa chỉ Slave (1-247)
 * @param function_code Mã hàm (0x03 hoặc 0x04)
 * @param start_reg Địa chỉ thanh ghi bắt đầu (0x0000 - 0xFFFF)
 * @param reg_count Số lượng thanh ghi cần đọc (1-125)
 * @param out_buf Bộ đệm đầu ra chứa gói tin Modbus hoàn chỉnh (kèm CRC)
 * @param out_len Độ dài gói tin (luôn là 8 bytes)
 */
esp_err_t innoedge_modbus_build_read_registers(uint8_t slave_addr, uint8_t function_code,
                                              uint16_t start_reg, uint16_t reg_count,
                                              uint8_t *out_buf, size_t *out_len);

/**
 * @brief Tạo gói tin Modbus RTU Ghi 1 Thanh Ghi (Write Single Register - 0x06)
 */
esp_err_t innoedge_modbus_build_write_single_register(uint8_t slave_addr, uint16_t reg_addr,
                                                     uint16_t value,
                                                     uint8_t *out_buf, size_t *out_len);

/**
 * @brief Phân tích gói tin phản hồi từ thiết bị Modbus Slave
 * @param slave_addr Địa chỉ Slave mong đợi
 * @param expected_fc Mã hàm mong đợi
 * @param rx_data Dữ liệu thô nhận được từ RS485
 * @param rx_len Độ dài dữ liệu nhận được
 * @param out_registers Mảng chứa các giá trị thanh ghi 16-bit đọc được
 * @param max_regs Kích thước mảng out_registers
 * @param out_reg_count Số thanh ghi thực tế đọc được
 */
esp_err_t innoedge_modbus_parse_response(uint8_t slave_addr, uint8_t expected_fc,
                                        const uint8_t *rx_data, size_t rx_len,
                                        uint16_t *out_registers, size_t max_regs,
                                        size_t *out_reg_count);

#ifdef __cplusplus
}
#endif
