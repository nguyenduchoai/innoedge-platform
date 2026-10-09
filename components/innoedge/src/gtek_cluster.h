// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

// innoedge_cluster_role_t định nghĩa MỘT chỗ ở API công khai — khai lại ở đây
// là lỗi "redeclaration" khi innoedge.c include cả hai.
#include "innoedge.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo cụm cluster với vai trò xác định
esp_err_t gtek_cluster_init(innoedge_cluster_role_t role);

// Lấy vai trò hiện tại
innoedge_cluster_role_t gtek_cluster_get_role(void);

// Đóng gói giao dịch từ máy Worker thành chuỗi JSON chuyển tiếp cho Master
esp_err_t gtek_cluster_pack_payment(const char *subnode_id, int kind, int count,
                                    int64_t amount_vnd, char *out, size_t out_len);

// Giải mã gói tin lệnh điều khiển từ Master chuyển xuống Worker
esp_err_t gtek_cluster_unpack_command(const char *json_str, char *target_subnode,
                                      size_t subnode_len, long long *cmd_id,
                                      char *action, size_t act_len,
                                      char *params_json, size_t params_len);

// Đóng gói phản hồi kết quả lệnh từ Worker gửi ngược lên Master
esp_err_t gtek_cluster_pack_command_ack(const char *subnode_id, long long cmd_id,
                                        const char *status, const char *msg,
                                        char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
