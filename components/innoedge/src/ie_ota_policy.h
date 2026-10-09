// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// Luật OTA thuần (không ESP-IDF) để test được trên máy host: parse digest hex,
// semver nghiêm ngặt, và quyết định có được cài bản ứng viên không.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline bool ie_ota_digest(const char *hex, uint8_t out[32])
{
    if (!hex || strlen(hex) != 64) return false;
    for (unsigned i = 0; i < 64; ++i) {
        char c = hex[i];
        int n = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (n < 0) return false;
        if (i % 2 == 0) out[i / 2] = (uint8_t)(n << 4);
        else out[i / 2] |= (uint8_t)n;
    }
    return true;
}

static inline bool ie_ota_semver(const char *s, uint32_t out[3])
{
    if (!s) return false;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t n = 0;
        if (*s < '0' || *s > '9') return false;
        do {
            if (n > 100000) return false;
            n = n * 10 + (uint32_t)(*s++ - '0');
        } while (*s >= '0' && *s <= '9');
        out[i] = n;
        if (i < 2 && *s++ != '.') return false;
    }
    return *s == '\0';
}

static inline bool ie_ota_version_allowed(const char *candidate, const char *current,
                                           bool allow_downgrade)
{
    uint32_t a[3], b[3];
    if (!ie_ota_semver(candidate, a) || !ie_ota_semver(current, b)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i] || allow_downgrade;
    }
    return false;
}
