// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// Test host cho ie_paid_guard — on_paid là tín hiệu DUY NHẤT được giao hàng, nên
// phải tới application đúng một lần mỗi intentId, qua cả reboot.

#include "ie_command_journal.h"
#include "ie_paid_guard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char g_blob[sizeof(ie_command_journal_t)];
static int g_exists, g_write_fails, g_faults;

esp_err_t ie_config_store_load_blob(const char *key, void *data, size_t len)
{
    assert(strcmp(key, "paid_journal") == 0 && len == sizeof(g_blob));
    if (!g_exists) return ESP_ERR_NOT_FOUND;
    memcpy(data, g_blob, len);
    return ESP_OK;
}

esp_err_t ie_config_store_save_blob(const char *key, const void *data, size_t len)
{
    assert(strcmp(key, "paid_journal") == 0 && len == sizeof(g_blob));
    if (g_write_fails) return ESP_FAIL;
    memcpy(g_blob, data, len);
    g_exists = 1;
    return ESP_OK;
}

void ie_fault_set(const char *c, const char *s, const char *m) { (void)c; (void)s; (void)m; g_faults++; }

static int g_delivered;

// Mô phỏng innoedge.c: DELIVER → giao + done; DUPLICATE → chỉ ack.
static ie_paid_decision_t paid(int64_t intent)
{
    size_t slot = 0;
    ie_paid_decision_t d = ie_paid_guard_begin(intent, &slot);
    if (d == IE_PAID_DELIVER) {
        g_delivered++;
        ie_paid_guard_done(slot);
    }
    return d;
}

static void boot(void) { g_faults = 0; g_write_fails = 0; ie_paid_guard_init(); }

int main(void)
{
    // 1. Lần đầu giao; cloud gửi lại (ack rơi) → KHÔNG giao lần hai.
    boot();
    assert(paid(88) == IE_PAID_DELIVER);
    assert(paid(88) == IE_PAID_DUPLICATE);
    assert(g_delivered == 1);

    // 2. Qua reboot vẫn nhớ.
    boot();
    assert(paid(88) == IE_PAID_DUPLICATE);
    assert(g_delivered == 1);

    // 3. Hai khách trả lệch thứ tự (88 rồi 87): 87 vẫn phải được giao.
    assert(paid(87) == IE_PAID_DELIVER);
    assert(g_delivered == 2);

    // 4. intentId sai → từ chối (không ack, không giao).
    assert(paid(0) == IE_PAID_REFUSE && paid(-1) == IE_PAID_REFUSE);

    // 5. Không ghi được NVS → không giao, không ack; NVS hồi phục → giao bình thường.
    g_write_fails = 1;
    assert(paid(90) == IE_PAID_REFUSE);
    g_write_fails = 0;
    assert(paid(90) == IE_PAID_DELIVER);
    assert(g_delivered == 3);

    // 6. Mất điện giữa on_paid: "đang giao" còn trong flash → reboot báo critical,
    //    frame gửi lại KHÔNG được giao lần nữa, và trả UNCERTAIN (không phải
    //    "đã giao" im lặng) để innoedge.c báo đối soát.
    size_t slot = 0;
    assert(ie_paid_guard_begin(91, &slot) == IE_PAID_DELIVER); // giao dở, chưa done
    boot();
    assert(g_faults == 1);
    assert(paid(91) == IE_PAID_UNCERTAIN);
    assert(g_delivered == 3);

    // 6b. Webhook tới muộn cho intent đã rơi khỏi cửa sổ 64 mục → UNCERTAIN, không
    //     bao giờ coi là "đã giao" im lặng.
    for (int64_t i = 1000; i < 1000 + IE_COMMAND_JOURNAL_SLOTS + 1; i++) {
        assert(paid(i) == IE_PAID_DELIVER);
    }
    int before = g_delivered;
    assert(paid(1000) == IE_PAID_UNCERTAIN);
    assert(g_delivered == before);

    // 7. Blob hỏng → vẫn chạy được (bắt đầu lại) + báo critical để đối soát.
    memset(g_blob, 0xAB, sizeof(g_blob));
    boot();
    assert(g_faults == 1);
    assert(paid(200) == IE_PAID_DELIVER);

    puts("PASS — 8 nhóm kiểm tra chống giao hàng QR hai lần");
    return 0;
}
