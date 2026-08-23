// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 01 — Hello Device: đưa một ESP32 lên InnoEdge Cloud.
//
// Đây là toàn bộ những gì cần để máy ONLINE. Không MQTT topic, không JSON,
// không retry thủ công — SDK lo hết.

#include "innoedge.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hello";

// Chưa có WiFi → SDK mở BLE/SoftAP tên "GTEK-Setup-XXXX" và dừng ở đây.
// Dùng app di động để cài WiFi cho máy.
static void on_provisioning(void)
{
    ESP_LOGW(TAG, "CHỜ CÀI WIFI — mở app, tìm thiết bị tên bắt đầu bằng GTEK-Setup");
}

static void on_assigned(void)
{
    ESP_LOGI(TAG, "Máy đã được gán cho đối tác — sẵn sàng phục vụ");
}

static void on_unassigned(void)
{
    ESP_LOGW(TAG, "Máy CHƯA gán đối tác — thêm máy trong app rồi quét mã kích hoạt");
}

void app_main(void)
{
    static const innoedge_events_t events = {
        .on_provisioning = on_provisioning,
        .on_assigned = on_assigned,
        .on_unassigned = on_unassigned,
    };
    innoedge_config_t cfg = {
        .fw_version = "0.1.0",
        .events = &events,
    };

    ESP_ERROR_CHECK(innoedge_init(&cfg));
    ESP_LOGI(TAG, "device_id (MAC) = %s — dùng mã này để thêm máy trên cloud",
             innoedge_device_id());
    ESP_ERROR_CHECK(innoedge_start());

    // innoedge_start() KHÔNG chặn: mọi thứ chạy nền. Vòng lặp này chỉ để nhìn
    // trạng thái trên serial monitor.
    while (true) {
        ESP_LOGI(TAG, "online=%s  assigned=%s  hàng đợi tồn=%u",
                 innoedge_is_online() ? "có" : "không",
                 innoedge_is_assigned() ? "rồi" : "chưa",
                 (unsigned)innoedge_queue_depth());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
