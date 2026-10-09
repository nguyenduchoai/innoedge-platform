// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_paid_guard.h"

#include "ie_command_journal.h"
#include "ie_config_store.h"
#include "ie_fault.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ie.paid";
#define PAID_KEY "paid_journal"

// Chỉ gọi từ task WS (frame payment_paid) nên không cần khoá.
static ie_command_journal_t s_paid;
static bool s_ready;

static void fresh(void)
{
    memset(&s_paid, 0, sizeof(s_paid));
    s_paid.version = IE_COMMAND_JOURNAL_VERSION;
}

void ie_paid_guard_init(void)
{
    esp_err_t err = ie_config_store_load_blob(PAID_KEY, &s_paid, sizeof(s_paid));
    if (err == ESP_ERR_NOT_FOUND) {
        fresh();
    } else if (err != ESP_OK || !ie_command_journal_valid(&s_paid)) {
        // Hỏng: bắt đầu lại. Intent cũ cloud gửi lại có thể được giao lần nữa —
        // báo để đối soát thay vì chặn luôn mọi khoản thanh toán mới.
        ESP_LOGE(TAG, "paid_journal hỏng (%s) — dựng lại", esp_err_to_name(err));
        ie_fault_set("paid_journal_reset", "critical",
                     "Nhat ky thanh toan QR hong - doi soat giao hang gan day");
        fresh();
    } else {
        for (size_t i = 0; i < IE_COMMAND_JOURNAL_SLOTS; i++) {
            if (s_paid.entries[i].state == IE_COMMAND_RUNNING) {
                ESP_LOGE(TAG, "intent=%lld đang giao thì mất điện",
                         (long long)s_paid.entries[i].id);
                ie_fault_set("paid_interrupted", "critical",
                             "Mat dien luc dang giao hang QR - can doi soat");
            }
        }
    }
    s_ready = true;
}

ie_paid_decision_t ie_paid_guard_begin(int64_t intent_id, size_t *slot)
{
    if (!s_ready || intent_id <= 0 || !slot) {
        return IE_PAID_REFUSE;
    }
    ie_command_state_t prev = ie_command_journal_lookup(&s_paid, intent_id);
    if (prev == IE_COMMAND_OK) {
        return IE_PAID_DUPLICATE;
    }
    if (prev != IE_COMMAND_UNSEEN) {
        return IE_PAID_UNCERTAIN;
    }
    *slot = ie_command_journal_begin(&s_paid, intent_id);
    if (ie_config_store_save_blob(PAID_KEY, &s_paid, sizeof(s_paid)) != ESP_OK) {
        // Không nhớ được = không giao. Không ack nên cloud sẽ gửi lại sau.
        s_paid.entries[*slot] = (ie_command_record_t){0};
        ie_fault_set("paid_journal_write_failed", "critical",
                     "Khong luu duoc thanh toan QR - tam dung giao hang");
        return IE_PAID_REFUSE;
    }
    return IE_PAID_DELIVER;
}

void ie_paid_guard_done(size_t slot)
{
    if (slot >= IE_COMMAND_JOURNAL_SLOTS) {
        return;
    }
    s_paid.entries[slot].state = IE_COMMAND_OK;
    if (ie_config_store_save_blob(PAID_KEY, &s_paid, sizeof(s_paid)) != ESP_OK) {
        ESP_LOGW(TAG, "không ghi được trạng thái đã giao (sẽ báo đối soát sau reboot)");
    }
}
