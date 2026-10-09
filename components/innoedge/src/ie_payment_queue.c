// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_payment_queue.h"

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ie.payq";
// ponytail: giữ tên namespace cũ — đổi là bỏ rơi giao dịch tồn trên bo đã flash.
static const char *NVS_NS = "gtek_payq";

static uint32_t s_head;
static uint32_t s_count;

static uint32_t queue_cap(void)
{
    uint32_t cap = CONFIG_INNOEDGE_PAYMENT_QUEUE_CAP;
    if (cap == 0 || cap > 999) {
        cap = 500;
    }
    return cap;
}

static void key_for(uint32_t idx, char key[8])
{
    snprintf(key, 8, "q%03u", (unsigned)idx);
}

static void load_meta(nvs_handle_t nvs)
{
    nvs_get_u32(nvs, "head", &s_head);
    nvs_get_u32(nvs, "count", &s_count);
    uint32_t cap = queue_cap();
    if (s_head >= cap) {
        s_head = 0;
    }
    if (s_count > cap) {
        s_count = cap;
    }
}

static esp_err_t save_meta(nvs_handle_t nvs)
{
    esp_err_t err = nvs_set_u32(nvs, "head", s_head);
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "count", s_count);
    }
    return err;
}

static uint64_t parse_seq(const char *json)
{
    uint64_t seq = 0;
    cJSON *root = cJSON_Parse(json);
    if (root) {
        cJSON *seq_j = cJSON_GetObjectItem(root, "seq");
        if (cJSON_IsNumber(seq_j)) {
            seq = (uint64_t)seq_j->valuedouble;
        }
        cJSON_Delete(root);
    }
    return seq;
}

esp_err_t ie_payment_queue_init(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    load_meta(nvs);
    err = save_meta(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "queue ready head=%u count=%u cap=%u",
             (unsigned)s_head, (unsigned)s_count, (unsigned)queue_cap());
    return err;
}

esp_err_t ie_payment_queue_append(uint64_t seq, const char *json, bool *out_dropped)
{
    if (out_dropped) {
        *out_dropped = false;
    }
    if (!json || json[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    load_meta(nvs);
    uint32_t cap = queue_cap();
    bool full = (s_count >= cap);
    // WRITE-THEN-EVICT: khi đầy, slot ghi mới CHÍNH là slot head (oldest) → ghi đè.
    // Ghi entry MỚI THÀNH CÔNG rồi mới cập nhật meta + commit 1 lần. Bản cũ erase
    // head TRƯỚC khi ghi → nếu ghi lỗi thì head bị xóa nhưng meta vẫn đầy = kẹt
    // drain (count=cap trỏ slot rỗng). Cách này: ghi lỗi → KHÔNG commit → giữ
    // nguyên trạng thái cũ (atomic).
    uint32_t idx = full ? s_head : (s_head + s_count) % cap;
    char key[8];
    key_for(idx, key);
    err = nvs_set_str(nvs, key, json);
    if (err != ESP_OK) {
        nvs_close(nvs);
        ESP_LOGE(TAG, "queue: ghi entry thất bại seq=%llu: %s",
                 (unsigned long long)seq, esp_err_to_name(err));
        return err;
    }
    if (full) {
        // Vừa ghi đè oldest → drop oldest (advance head), count giữ nguyên = cap.
        s_head = (s_head + 1) % cap;
        ESP_LOGE(TAG, "queue full, dropped oldest payment event");
        if (out_dropped) {
            *out_dropped = true; // caller PHẢI phát cảnh báo critical (mất tiền)
        }
    } else {
        s_count++;
    }
    err = save_meta(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "queued seq=%llu count=%u", (unsigned long long)seq, (unsigned)s_count);
    return err;
}

esp_err_t ie_payment_queue_peek(ie_payment_event_t *event)
{
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(event, 0, sizeof(*event));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    load_meta(nvs);
    if (s_count == 0) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }
    char key[8];
    key_for(s_head, key);
    size_t len = sizeof(event->json);
    err = nvs_get_str(nvs, key, event->json, &len);
    nvs_close(nvs);
    if (err == ESP_OK) {
        event->seq = parse_seq(event->json);
    }
    return err;
}

esp_err_t ie_payment_queue_ack(uint64_t seq)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    load_meta(nvs);
    if (s_count == 0) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }
    char key[8];
    char json[192] = {0};
    key_for(s_head, key);
    size_t len = sizeof(json);
    err = nvs_get_str(nvs, key, json, &len);
    if (err == ESP_OK && parse_seq(json) == seq) {
        nvs_erase_key(nvs, key);
        s_head = (s_head + 1) % queue_cap();
        s_count--;
        err = save_meta(nvs);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        ESP_LOGI(TAG, "ack seq=%llu remaining=%u", (unsigned long long)seq, (unsigned)s_count);
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "ack seq=%llu ignored; queue head is different", (unsigned long long)seq);
        err = ESP_ERR_NOT_FOUND;
    }
    nvs_close(nvs);
    return err;
}

uint32_t ie_payment_queue_count(void)
{
    return s_count;
}
