// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_asset_client.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ie.assets";

#define ASSET_MAX_BLOB (768 * 1024) // banner KM full-middle 460x350 RGB565+alpha ~487KB (PSRAM)
#define ASSET_SLOTS 3 // 0=logo G-TEK, 1=logo đối tác, 2=banner KM màn chờ

static ie_logo_asset_t s_logos[ASSET_SLOTS];
static uint8_t *s_blob; // buffer đang giữ pixel data của s_logos

const ie_logo_asset_t *ie_asset_logo(int slot)
{
    if (slot < 0 || slot >= ASSET_SLOTS || s_logos[slot].rgb565 == NULL) {
        return NULL;
    }
    return &s_logos[slot];
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Parse blob GLG1 → trỏ s_logos vào trong blob (zero-copy). Blob cũ chỉ được
// giải phóng SAU khi swap xong để reader không cầm con trỏ chết quá 1 frame.
static esp_err_t parse_blob(uint8_t *blob, size_t len)
{
    if (len < 5 || memcmp(blob, "GLG1", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ie_logo_asset_t logos[ASSET_SLOTS] = {0};
    size_t off = 4;
    uint8_t count = blob[off++];
    for (uint8_t i = 0; i < count; i++) {
        if (off + 5 > len) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint8_t slot = blob[off];
        uint16_t w = rd16(blob + off + 1);
        uint16_t h = rd16(blob + off + 3);
        off += 5;
        size_t px_bytes = (size_t)w * h * 2;
        size_t a_bytes = (size_t)w * h;
        // h tới 480: slot 2 = banner KM phủ khúc giữa (logo header chỉ ~40px).
        if (w == 0 || h == 0 || w > 480 || h > 480 || off + px_bytes + a_bytes > len) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (slot < ASSET_SLOTS) {
            logos[slot].width = w;
            logos[slot].height = h;
            logos[slot].rgb565 = (const uint16_t *)(blob + off); // LE khớp ESP32
            logos[slot].alpha = blob + off + px_bytes;
        }
        off += px_bytes + a_bytes;
    }
    uint8_t *old = s_blob;
    memcpy(s_logos, logos, sizeof(logos));
    s_blob = blob;
    free(old);
    return ESP_OK;
}

esp_err_t ie_asset_client_fetch(const ie_device_config_t *config)
{
    if (!config || config->server_base_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char url[224];
    snprintf(url, sizeof(url), "%s/device-assets/header", config->server_base_url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&http_cfg);
    if (!cli) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(cli, "Device-Id", config->device_id);
    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }
    int64_t content_len = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    // Qua nginx response thường là CHUNKED (không Content-Length) → đọc tới
    // EOF với buffer trần ASSET_MAX_BLOB thay vì tin content_len.
    if (status != 200 || content_len > ASSET_MAX_BLOB) {
        ESP_LOGW(TAG, "asset fetch HTTP %d len=%lld", status, (long long)content_len);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }
    uint8_t *blob = heap_caps_malloc(ASSET_MAX_BLOB,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) {
        blob = malloc(ASSET_MAX_BLOB);
    }
    if (!blob) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }
    size_t got = 0;
    while (got < ASSET_MAX_BLOB) {
        int n = esp_http_client_read(cli, (char *)blob + got,
                                     (int)(ASSET_MAX_BLOB - got));
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    if (got < 5 || (content_len > 0 && got != (size_t)content_len)) {
        free(blob);
        ESP_LOGW(TAG, "asset fetch thiếu dữ liệu got=%u", (unsigned)got);
        return ESP_FAIL;
    }
    // Thu gọn buffer về đúng cỡ blob (trả lại PSRAM thừa).
    uint8_t *shrunk = heap_caps_malloc(got, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!shrunk) {
        shrunk = malloc(got);
    }
    if (shrunk) {
        memcpy(shrunk, blob, got);
        free(blob);
        blob = shrunk;
    }
    err = parse_blob(blob, got);
    if (err != ESP_OK) {
        free(blob);
        ESP_LOGW(TAG, "asset blob không hợp lệ: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "header assets: trái=%s phải=%s",
             s_logos[0].rgb565 ? "có" : "mặc định",
             s_logos[1].rgb565 ? "có" : "không");
    return ESP_OK;
}
