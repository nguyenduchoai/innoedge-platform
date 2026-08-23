// 02 — Telemetry: gửi tiền + cảnh báo lên cloud, KHÔNG mất dữ liệu khi rớt mạng.
//
// Nhấn nút BOOT (GPIO0) = giả lập khách bỏ 1 xu.
// Giữ BOOT > 2s          = giả lập sự cố (alert), thả ra = báo hết sự cố.
//
// Thử rút WiFi rồi bấm vài lần: hàng đợi tăng, cắm lại mạng là tự đẩy hết lên.

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "telemetry";

#define BOOT_BUTTON_GPIO 0
#define HOLD_ALERT_MS    2000

static void on_payment_ack(const char *method, int coins, int64_t amount_vnd,
                           int64_t rate_vnd, bool duplicate)
{
    // duplicate = frame này đã ghi nhận từ trước (SDK gửi lại sau khi mất mạng).
    // Cloud dedupe theo seq nên tiền KHÔNG bị đếm hai lần.
    ESP_LOGI(TAG, "cloud đã ghi: method=%s coins=%d amount=%lld đơn giá=%lld%s",
             method, coins, (long long)amount_vnd, (long long)rate_vnd,
             duplicate ? " (trùng — đã ghi trước đó)" : "");
}

void app_main(void)
{
    static const innoedge_events_t events = { .on_payment_ack = on_payment_ack };
    innoedge_config_t cfg = { .fw_version = "0.1.0", .events = &events };

    ESP_ERROR_CHECK(innoedge_init(&cfg));
    ESP_ERROR_CHECK(innoedge_start());

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    bool alert_on = false;
    uint32_t held_ms = 0;
    bool was_down = false;

    while (true) {
        bool down = gpio_get_level(BOOT_BUTTON_GPIO) == 0; // active-low
        if (down) {
            held_ms += 50;
            if (!alert_on && held_ms >= HOLD_ALERT_MS) {
                alert_on = true;
                // code là ĐỊNH DANH máy-đọc; message là câu người-đọc hiện trên app.
                innoedge_alert("coin_jam", "critical", "Ket xu - can kiem tra", true);
                ESP_LOGW(TAG, "đã báo sự cố coin_jam lên cloud");
            }
        } else {
            if (was_down && held_ms < HOLD_ALERT_MS) {
                // Bấm ngắn = 1 xu. LUÔN vào NVS trước khi gửi → mất điện ngay
                // lúc này cũng không mất giao dịch.
                esp_err_t err = innoedge_publish_payment(INNOEDGE_PAY_COIN, 1, 0);
                ESP_LOGI(TAG, "ghi nhận 1 xu (%s) — tồn %u giao dịch chưa gửi",
                         esp_err_to_name(err), (unsigned)innoedge_queue_depth());
            }
            if (alert_on) {
                alert_on = false;
                innoedge_alert("coin_jam", NULL, NULL, false); // active=false = đã hết lỗi
                ESP_LOGI(TAG, "đã báo HẾT sự cố coin_jam");
            }
            held_ms = 0;
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
