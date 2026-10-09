// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once
//
// Nhật ký lệnh có side-effect, bền qua reboot (blob NVS ~1 KB).
//
// Thay high-watermark một số: watermark coi MỌI id <= max là "đã chạy", nên lệnh
// tới lệch thứ tự (11 trước 10) làm 10 bị bỏ mà vẫn ack "ok" — khách trả tiền,
// máy không nhả. Journal nhớ trạng thái TỪNG id trong 64 lệnh gần nhất:
//   RUNNING  ghi TRƯỚC khi chạm phần cứng; còn RUNNING sau reboot = không chắc
//            đã chạy hay chưa → báo đối soát, KHÔNG tự chạy lại, KHÔNG ack "ok".
//   OK/FAILED ghi TRƯỚC khi ack.
// Id rơi khỏi cửa sổ (<= retired_through) cũng là "không chắc", không bao giờ
// thành "đã chạy xong" để ack ok.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IE_COMMAND_JOURNAL_SLOTS 64
#define IE_COMMAND_JOURNAL_VERSION 1

typedef enum {
    IE_COMMAND_UNSEEN = 0,
    IE_COMMAND_RUNNING = 1,
    IE_COMMAND_OK = 2,
    IE_COMMAND_FAILED = 3,
    IE_COMMAND_TOO_OLD = 4,
} ie_command_state_t;

typedef struct {
    int64_t id;
    uint64_t state;
} ie_command_record_t;

typedef struct {
    int64_t retired_through;
    uint32_t version;
    uint32_t next;
    ie_command_record_t entries[IE_COMMAND_JOURNAL_SLOTS];
} ie_command_journal_t;

static inline bool ie_command_journal_valid(const ie_command_journal_t *j)
{
    if (j->version != IE_COMMAND_JOURNAL_VERSION || j->next >= IE_COMMAND_JOURNAL_SLOTS || j->retired_through < 0) {
        return false;
    }
    for (size_t i = 0; i < IE_COMMAND_JOURNAL_SLOTS; i++) {
        if (j->entries[i].id < 0 || j->entries[i].state > IE_COMMAND_FAILED ||
            ((j->entries[i].id == 0) != (j->entries[i].state == IE_COMMAND_UNSEEN))) {
            return false;
        }
    }
    return true;
}

static inline ie_command_state_t ie_command_journal_lookup(const ie_command_journal_t *j, int64_t id)
{
    if (id <= 0) {
        return IE_COMMAND_TOO_OLD;
    }
    for (size_t i = 0; i < IE_COMMAND_JOURNAL_SLOTS; i++) {
        if (j->entries[i].id == id) {
            return (ie_command_state_t)j->entries[i].state;
        }
    }
    return id <= j->retired_through ? IE_COMMAND_TOO_OLD : IE_COMMAND_UNSEEN;
}

// Id lớn nhất journal biết (kể cả đã rơi khỏi cửa sổ). Watermark last_cmd chỉ
// được vượt con số này khi một firmware KHÁC (bản cũ, lúc rollback) đã chạy lệnh.
static inline int64_t ie_command_journal_max_id(const ie_command_journal_t *j)
{
    int64_t max = j->retired_through;
    for (size_t i = 0; i < IE_COMMAND_JOURNAL_SLOTS; i++) {
        if (j->entries[i].id > max) {
            max = j->entries[i].id;
        }
    }
    return max;
}

// Caller persists RUNNING before touching hardware, and the final state before ACK.
static inline size_t ie_command_journal_begin(ie_command_journal_t *j, int64_t id)
{
    size_t slot = j->next;
    if (j->entries[slot].id > j->retired_through) {
        j->retired_through = j->entries[slot].id;
    }
    j->entries[slot] = (ie_command_record_t){.id = id, .state = IE_COMMAND_RUNNING};
    j->next = (slot + 1) % IE_COMMAND_JOURNAL_SLOTS;
    return slot;
}
