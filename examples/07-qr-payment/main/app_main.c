// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 07 — QR Payment: khách quét QR trả tiền, máy biết ngay khi tiền về.
//
// Nhấn BOOT = khách chọn gói 20.000đ → xin QR → chờ báo đã trả.
//
// Máy KHÔNG tự dựng chuỗi QR. Cloud giữ cấu hình cổng thanh toán của từng đối
// tác (Pay2S/9Pay/MoMo/bank) và trả về chuỗi đã sẵn sàng — firmware render
// nguyên văn.

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "qr";

#define BOOT_BUTTON_GPIO 0
#define PRICE_VND        20000

// QR đã sẵn sàng. Render `payload` NGUYÊN VĂN thành mã QR — TUYỆT ĐỐI không
// parse hay dựng lại chuỗi: nội dung khác nhau theo cổng (EMV VietQR, URL ví...).
static void on_qr(const char *payload, int64_t amount_vnd, const char *ref_code,
                  int expires_sec, int64_t intent_id)
{
    ESP_LOGI(TAG, "QR %lldđ · mã %s · hết hạn sau %ds · intent=%lld",
             (long long)amount_vnd, ref_code, expires_sec, (long long)intent_id);
    ESP_LOGI(TAG, "payload: %s", payload);
    // Máy có màn hình: gtek_ui_app_show_qr(payload, ...) hoặc thư viện QR bất kỳ.
}

// Cổng thanh toán lỗi và cloud KHÔNG có kênh dự phòng nào. Cloud cố tình không
// phát QR "vô chủ" — khách chuyển tiền mà không ai xác nhận là mất tiền thật.
// Việc của máy: hiện đúng `message` cho khách + nút thử lại.
static void on_qr_error(const char *message)
{
    ESP_LOGE(TAG, "không tạo được QR: %s", message ? message : "(không rõ)");
}

// Webhook ngân hàng/ví đã xác nhận tiền về. ĐÂY là tín hiệu duy nhất được phép
// dùng để giao hàng — không phải "khách bảo đã chuyển".
static void on_paid(int64_t intent_id, int64_t amount_vnd)
{
    ESP_LOGI(TAG, "ĐÃ THANH TOÁN %lldđ (intent=%lld) — bắt đầu phục vụ",
             (long long)amount_vnd, (long long)intent_id);
    // ... mở relay / chạy máy / in bill ...
}

// QR tĩnh của máy — cloud đẩy xuống, SDK cache NVS. Hiện ở màn chờ để khách
// chuyển khoản kể cả khi máy chưa xin QR động.
static void on_static_qr(const char *payload, const char *ref_code)
{
    ESP_LOGI(TAG, "QR tĩnh của máy (mã %s): %s", ref_code, payload);
}

void app_main(void)
{
    static const innoedge_events_t events = {
        .on_qr = on_qr,
        .on_qr_error = on_qr_error,
        .on_paid = on_paid,
        .on_static_qr = on_static_qr,
    };
    innoedge_config_t cfg = { .fw_version = "0.1.0", .events = &events };

    ESP_ERROR_CHECK(innoedge_init(&cfg));
    ESP_ERROR_CHECK(innoedge_start());

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    bool was_down = false;
    while (true) {
        bool down = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        if (down && !was_down) {
            esp_err_t err = innoedge_request_qr(PRICE_VND);
            if (err != ESP_OK) {
                // QR động BẮT BUỘC online (khác tiền mặt — tiền mặt vào hàng đợi).
                ESP_LOGW(TAG, "chưa xin được QR: %s — cần máy đã gán + đang online",
                         esp_err_to_name(err));
            }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
