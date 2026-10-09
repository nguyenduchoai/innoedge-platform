// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_fault.h"
#include "ie_ws_client.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "ie.fault";

// Latch các code đang active (chống gửi lặp). Đủ lớn cho mọi loại lỗi firmware.
#define IE_FAULT_MAX 32
#define IE_FAULT_CODE_LEN 40

static char s_active[IE_FAULT_MAX][IE_FAULT_CODE_LEN];

static int find_slot(const char *code)
{
    for (int i = 0; i < IE_FAULT_MAX; i++) {
        if (s_active[i][0] && strncmp(s_active[i], code, IE_FAULT_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static int free_slot(void)
{
    for (int i = 0; i < IE_FAULT_MAX; i++) {
        if (!s_active[i][0]) {
            return i;
        }
    }
    return -1;
}

void ie_fault_set(const char *code, const char *severity, const char *message)
{
    if (!code || !code[0]) {
        return;
    }
    if (find_slot(code) >= 0) {
        return; // đã active → không gửi lại (chống spam)
    }
    int slot = free_slot();
    if (slot >= 0) {
        strncpy(s_active[slot], code, IE_FAULT_CODE_LEN - 1);
        s_active[slot][IE_FAULT_CODE_LEN - 1] = '\0';
    }
    ESP_LOGW(TAG, "FAULT set: %s (%s) %s", code, severity ? severity : "warning",
             message ? message : "");
    ie_ws_send_alert(code, severity ? severity : "warning", message, true);
}

void ie_fault_clear(const char *code)
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
    ie_ws_send_alert(code, NULL, NULL, false);
}
