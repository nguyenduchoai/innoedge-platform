// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_blackbox.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_system.h"
#include "esp_timer.h"
#else
static uint32_t s_mock_uptime = 100;
static int s_mock_reset_reason = 0;
void gtek_blackbox_set_mock_reset_reason(int reason) { s_mock_reset_reason = reason; }
#endif

static gtek_breadcrumb_t s_crumbs[GTEK_BLACKBOX_MAX_BREADCRUMBS];
static size_t s_crumb_head = 0;
static size_t s_crumb_count = 0;

static bool s_has_pending_report = false;
static int s_recorded_reset_reason = 0;
static char s_last_error_code[32] = {0};
static char s_last_error_details[64] = {0};

static uint32_t get_uptime(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
#else
    return s_mock_uptime++;
#endif
}

esp_err_t gtek_blackbox_init(void)
{
    memset(s_crumbs, 0, sizeof(s_crumbs));
    s_crumb_head = 0;
    s_crumb_count = 0;

#ifdef ESP_PLATFORM
    s_recorded_reset_reason = (int)esp_reset_reason();
    // 4 = PANIC, 5 = TASK_WDT, 6 = INT_WDT, 7 = WDT, 9 = BROWNOUT
    if (s_recorded_reset_reason == 4 || s_recorded_reset_reason == 5 ||
        s_recorded_reset_reason == 6 || s_recorded_reset_reason == 7 ||
        s_recorded_reset_reason == 9) {
        s_has_pending_report = true;
    }
#else
    s_recorded_reset_reason = s_mock_reset_reason;
    if (s_recorded_reset_reason > 0) {
        s_has_pending_report = true;
    }
#endif

    gtek_blackbox_record_breadcrumb("SYSTEM", "blackbox_init");
    return ESP_OK;
}

void gtek_blackbox_record_breadcrumb(const char *tag, const char *info)
{
    if (!tag) tag = "UNKNOWN";
    if (!info) info = "";

    gtek_breadcrumb_t *b = &s_crumbs[s_crumb_head];
    snprintf(b->tag, sizeof(b->tag), "%s", tag);
    snprintf(b->info, sizeof(b->info), "%s", info);
    b->uptime_sec = get_uptime();

    s_crumb_head = (s_crumb_head + 1) % GTEK_BLACKBOX_MAX_BREADCRUMBS;
    if (s_crumb_count < GTEK_BLACKBOX_MAX_BREADCRUMBS) {
        s_crumb_count++;
    }
}

esp_err_t gtek_blackbox_record_error(const char *code, const char *details)
{
    if (!code) return ESP_ERR_INVALID_ARG;
    snprintf(s_last_error_code, sizeof(s_last_error_code), "%s", code);
    snprintf(s_last_error_details, sizeof(s_last_error_details), "%s", details ? details : "");
    s_has_pending_report = true;

    gtek_blackbox_record_breadcrumb("ERROR", code);
    return ESP_OK;
}

bool gtek_blackbox_has_pending_report(void)
{
    return s_has_pending_report;
}

void gtek_blackbox_clear_report(void)
{
    s_has_pending_report = false;
    s_last_error_code[0] = '\0';
    s_last_error_details[0] = '\0';
}

static const char *reset_reason_to_name(int r)
{
    switch (r) {
    case 1:  return "POWERON";
    case 3:  return "SW_RESET";
    case 4:  return "PANIC";
    case 5:  return "TASK_WDT";
    case 6:  return "INT_WDT";
    case 7:  return "WDT";
    case 9:  return "BROWNOUT";
    default: return "OTHER";
    }
}

esp_err_t gtek_blackbox_format_report(char *out, size_t max_len)
{
    if (!out || max_len < 64) return ESP_ERR_INVALID_ARG;

    int written = snprintf(out, max_len,
                           "{\"type\":\"diagnostic\",\"reset_reason\":%d,\"reset_name\":\"%s\","
                           "\"last_error\":\"%s\",\"error_details\":\"%s\",\"uptime\":%lu,"
                           "\"breadcrumbs\":[",
                           s_recorded_reset_reason,
                           reset_reason_to_name(s_recorded_reset_reason),
                           s_last_error_code,
                           s_last_error_details,
                           (unsigned long)get_uptime());
    if (written < 0 || (size_t)written >= max_len) return ESP_ERR_NO_MEM;

    size_t pos = (size_t)written;
    for (size_t i = 0; i < s_crumb_count; i++) {
        // Thứ tự từ cũ tới mới
        size_t idx;
        if (s_crumb_count < GTEK_BLACKBOX_MAX_BREADCRUMBS) {
            idx = i;
        } else {
            idx = (s_crumb_head + i) % GTEK_BLACKBOX_MAX_BREADCRUMBS;
        }
        gtek_breadcrumb_t *b = &s_crumbs[idx];
        written = snprintf(out + pos, max_len - pos,
                           "%s{\"tag\":\"%s\",\"info\":\"%s\",\"t\":%lu}",
                           (i > 0 ? "," : ""),
                           b->tag, b->info, (unsigned long)b->uptime_sec);
        if (written < 0 || (size_t)written >= max_len - pos) break;
        pos += (size_t)written;
    }

    if (pos + 3 < max_len) {
        out[pos++] = ']';
        out[pos++] = '}';
        out[pos] = '\0';
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}

size_t gtek_blackbox_get_breadcrumbs(gtek_breadcrumb_t *out_arr, size_t max_count)
{
    if (!out_arr || max_count == 0) return 0;
    size_t n = (s_crumb_count < max_count) ? s_crumb_count : max_count;
    for (size_t i = 0; i < n; i++) {
        size_t idx;
        if (s_crumb_count < GTEK_BLACKBOX_MAX_BREADCRUMBS) {
            idx = i;
        } else {
            idx = (s_crumb_head + i) % GTEK_BLACKBOX_MAX_BREADCRUMBS;
        }
        out_arr[i] = s_crumbs[idx];
    }
    return n;
}
