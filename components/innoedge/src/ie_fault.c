// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_fault.h"
#include "ie_ws_client.h"

#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ie.fault";

// Cảnh báo đang active: latch chống gửi lặp, và GIỮ NỘI DUNG để gửi lại khi WS
// kết nối. Lỗi phát lúc boot (lệnh bị ngắt khi mất điện, journal hỏng…) xảy ra
// TRƯỚC khi có mạng — không giữ lại thì cloud không bao giờ biết.
#define IE_FAULT_MAX 24
#define IE_FAULT_CODE_LEN 40
#define IE_FAULT_SEV_LEN 12
#define IE_FAULT_MSG_LEN 80

typedef struct {
    char code[IE_FAULT_CODE_LEN];
    char severity[IE_FAULT_SEV_LEN];
    char message[IE_FAULT_MSG_LEN];
} fault_t;

static fault_t s_active[IE_FAULT_MAX];

static int find_slot(const char *code)
{
    for (int i = 0; i < IE_FAULT_MAX; i++) {
        if (s_active[i].code[0] && strncmp(s_active[i].code, code, IE_FAULT_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static int free_slot(void)
{
    for (int i = 0; i < IE_FAULT_MAX; i++) {
        if (!s_active[i].code[0]) {
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
    severity = severity ? severity : "warning";
    int slot = free_slot();
    if (slot >= 0) {
        fault_t *f = &s_active[slot];
        snprintf(f->code, sizeof(f->code), "%s", code);
        snprintf(f->severity, sizeof(f->severity), "%s", severity);
        snprintf(f->message, sizeof(f->message), "%s", message ? message : "");
    }
    ESP_LOGW(TAG, "FAULT set: %s (%s) %s", code, severity, message ? message : "");
    ie_ws_send_alert(code, severity, message, true); // offline: gửi lại lúc kết nối
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
    s_active[slot].code[0] = '\0';
    ESP_LOGI(TAG, "FAULT clear: %s", code);
    ie_ws_send_alert(code, NULL, NULL, false);
}

void ie_fault_resend_active(void)
{
    for (int i = 0; i < IE_FAULT_MAX; i++) {
        const fault_t *f = &s_active[i];
        if (f->code[0]) {
            ie_ws_send_alert(f->code, f->severity, f->message, true); // cloud gom theo code
        }
    }
}
