// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_fault.h"
#include "gtek_ws_client.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "gtek.fault";

// Latch các code đang active (chống gửi lặp). Đủ lớn cho mọi loại lỗi firmware.
#define GTEK_FAULT_MAX 32
#define GTEK_FAULT_CODE_LEN 40

static char s_active[GTEK_FAULT_MAX][GTEK_FAULT_CODE_LEN];

static int find_slot(const char *code)
{
    for (int i = 0; i < GTEK_FAULT_MAX; i++) {
        if (s_active[i][0] && strncmp(s_active[i], code, GTEK_FAULT_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static int free_slot(void)
{
    for (int i = 0; i < GTEK_FAULT_MAX; i++) {
        if (!s_active[i][0]) {
            return i;
        }
    }
    return -1;
}

void gtek_fault_set(const char *code, const char *severity, const char *message)
{
    if (!code || !code[0]) {
        return;
    }
    if (find_slot(code) >= 0) {
        return; // đã active → không gửi lại (chống spam)
    }
    int slot = free_slot();
    if (slot >= 0) {
        strncpy(s_active[slot], code, GTEK_FAULT_CODE_LEN - 1);
        s_active[slot][GTEK_FAULT_CODE_LEN - 1] = '\0';
    }
    ESP_LOGW(TAG, "FAULT set: %s (%s) %s", code, severity ? severity : "warning",
             message ? message : "");
    gtek_ws_send_alert(code, severity ? severity : "warning", message, true);
}

void gtek_fault_clear(const char *code)
{
    if (!code || !code[0]) {
        return;
    }
    int slot = find_slot(code);
    if (slot < 0) {
        return; // chưa active → không gửi clear
    }
    s_active[slot][0] = '\0';
    ESP_LOGI(TAG, "FAULT clear: %s", code);
    gtek_ws_send_alert(code, NULL, NULL, false);
}
