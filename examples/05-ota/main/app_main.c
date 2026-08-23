// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 05 — OTA: cập nhật firmware từ xa, an toàn.
//
// Bạn không phải viết gì cho OTA. SDK tự: kiểm tra bản mới lúc boot → tải HTTPS
// → kiểm SHA-256 → ghi partition dự phòng → reboot → vào được cloud thì xác nhận
// (huỷ rollback). Bản mới treo/crash trước khi vào cloud → bootloader tự quay
// bản cũ ở lần reboot kế.
//
// Việc DUY NHẤT của application: nói cho SDK biết lúc nào máy ĐANG PHỤC VỤ
// KHÁCH để đừng reboot giữa chừng.

#include "innoedge.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>

static const char *TAG = "ota";

// Cờ "đang phục vụ khách". Đặt true khi bắt đầu giao dịch, false khi xong.
static atomic_bool s_serving;

// SDK hỏi trước khi tải/reboot OTA. true = hoãn, thử lại lần kiểm tra sau.
// PHẢI nhanh và không block — được gọi từ task OTA.
static bool is_busy(void)
{
    return atomic_load(&s_serving);
}

// Giả lập vòng đời phục vụ khách: 20s rảnh, 10s bận.
static void fake_customer_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20000));
        atomic_store(&s_serving, true);
        ESP_LOGW(TAG, "ĐANG PHỤC VỤ KHÁCH — OTA sẽ hoãn");
        vTaskDelay(pdMS_TO_TICKS(10000));
        atomic_store(&s_serving, false);
        ESP_LOGI(TAG, "khách xong — OTA được phép chạy");
    }
}

void app_main(void)
{
    innoedge_config_t cfg = {
        .fw_version = "0.1.0",  // cloud so version này để quyết định có bản mới không
        .busy_check = is_busy,  // bỏ NULL = cập nhật ngay khi có bản mới
    };
    ESP_ERROR_CHECK(innoedge_init(&cfg));
    ESP_ERROR_CHECK(innoedge_start());

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "đang chạy: %s (build %s %s)", app->version, app->date, app->time);

    xTaskCreate(fake_customer_task, "fake_customer", 3072, NULL, 3, NULL);

    // Kiểm tra thủ công mỗi 6 giờ. SDK đã tự kiểm tra lúc boot và khi cloud gửi
    // lệnh "ota_check", nên vòng này chỉ để máy chạy liên tục nhiều ngày.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(6 * 60 * 60 * 1000));
        ESP_LOGI(TAG, "kiểm tra bản mới định kỳ");
        innoedge_ota_check();
    }
}
