// 06 — Coin & Relay: máy coin-op thật.
//
// Đầu vào : đầu đọc xu/bill nhả xung → đếm → báo tiền lên cloud.
// Đầu ra  : cloud gửi lệnh "dispense" → nhả relay đúng số xung.
//
// Đây là ví dụ ĐẦY ĐỦ của một máy vận hành bằng xu — hai chiều tiền.

#include "innoedge.h"

#include "gtek_pulse_input.h"
#include "gtek_relay_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>

static const char *TAG = "coinop";

// Đầu đọc nhả xung xong (đã gom cả chuỗi xung + chống dội). Chạy trên task của
// pulse_input — ngắn gọn, đừng block.
static void on_pulse(gtek_pulse_kind_t kind, uint32_t pulses, int64_t amount_vnd,
                     void *ctx)
{
    (void)ctx;
    if (kind == GTEK_PULSE_COIN && amount_vnd <= 0) {
        // Đầu đọc xu: gửi SỐ XU, cloud quy đổi theo đơn giá của từng đối tác.
        // Đừng tự nhân giá ở firmware — mỗi đối tác một giá, đổi được từ app.
        ESP_LOGI(TAG, "khách bỏ %u xu", (unsigned)pulses);
        innoedge_publish_payment(INNOEDGE_PAY_COIN, (int)pulses, 0);
    } else {
        if (amount_vnd <= 0) {
            amount_vnd = (int64_t)pulses * CONFIG_GTEK_BILL_VND_PER_PULSE;
        }
        ESP_LOGI(TAG, "khách bỏ %lld đ tiền mặt", (long long)amount_vnd);
        innoedge_publish_payment(INNOEDGE_PAY_CASH, 0, amount_vnd);
    }
}

// "dispense": nhả tiền/credit ra cho khách. params {"amountVnd":20000}.
// Lệnh CÓ TIỀN → dựa hoàn toàn vào chống-trùng của SDK (xem README).
static esp_err_t cmd_dispense(cJSON *params, char *result, size_t result_len,
                              char *msg, size_t msg_len)
{
    cJSON *amount = params ? cJSON_GetObjectItem(params, "amountVnd") : NULL;
    if (!cJSON_IsNumber(amount) || amount->valuedouble <= 0) {
        snprintf(msg, msg_len, "thieu amountVnd");
        return ESP_ERR_INVALID_ARG;
    }

    int per_pulse = CONFIG_GTEK_DISPENSE_VND_PER_PULSE > 0
                        ? CONFIG_GTEK_DISPENSE_VND_PER_PULSE
                        : CONFIG_GTEK_BILL_VND_PER_PULSE;
    int pulses = (int)(amount->valuedouble / per_pulse);
    if (pulses <= 0) {
        snprintf(msg, msg_len, "so tien nho hon 1 xung");
        return ESP_ERR_INVALID_ARG;
    }
    // Trần an toàn: một params sai (hoặc bị sửa) không được biến thành 10.000
    // xung nhả sạch hopper.
    if (pulses > CONFIG_GTEK_DISPENSE_MAX_PULSES) {
        pulses = CONFIG_GTEK_DISPENSE_MAX_PULSES;
        ESP_LOGW(TAG, "vượt trần — cắt còn %d xung", pulses);
    }

    for (int i = 0; i < pulses; i++) {
        gtek_relay_control_pulse(0); // 0 = dùng CONFIG_GTEK_RELAY_PULSE_MS
        vTaskDelay(pdMS_TO_TICKS(CONFIG_GTEK_RELAY_PULSE_GAP_MS));
    }

    snprintf(result, result_len, "{\"pulses\":%d}", pulses);
    snprintf(msg, msg_len, "da nha %d xung", pulses);
    return ESP_OK;
}

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.0" };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    ESP_ERROR_CHECK(gtek_relay_control_init()); // mọi relay OFF khi boot
    ESP_ERROR_CHECK(gtek_pulse_input_init(on_pulse, NULL));
    ESP_ERROR_CHECK(innoedge_register_command("dispense", cmd_dispense));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "máy coin-op sẵn sàng (xu vào GPIO%d, relay ra GPIO%d)",
             CONFIG_GTEK_COIN_PULSE_GPIO, CONFIG_GTEK_RELAY_GPIO);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
