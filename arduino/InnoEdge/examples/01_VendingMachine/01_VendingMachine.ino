// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
//
// InnoEdge Arduino Example: 01_VendingMachine
// Máy bán hàng tự động nhận tiền mặt/xu và mã thanh toán QR.

#include <Arduino.h>
#include <InnoEdge.h>

#define RELAY_DISPENSE_PIN 4
#define COIN_INPUT_PIN     5

// Chống rung nút/xung
volatile unsigned long lastPulseTime = 0;
volatile int pendingCoins = 0;

void IRAM_ATTR onCoinPulse() {
    unsigned long now = millis();
    if (now - lastPulseTime > 30) { // debounce 30ms
        pendingCoins++;
        lastPulseTime = now;
    }
}

// Handler nhận lệnh nhả tiền / nhả hàng từ xa qua Cloud
esp_err_t handleDispense(cJSON *params, char *result_out, size_t result_len, char *msg_out, size_t msg_len) {
    int64_t amountVnd = 0;
    if (params) {
        cJSON *amt = cJSON_GetObjectItem(params, "amountVnd");
        if (cJSON_IsNumber(amt)) amountVnd = (int64_t)amt->valuedouble;
    }
    Serial.printf("[LỆNH TỪ CLOUD] Yêu cầu nhả hàng trị giá: %lld đ\n", amountVnd);

    // Kích relay nhả hàng trong 300ms
    digitalWrite(RELAY_DISPENSE_PIN, HIGH);
    delay(300);
    digitalWrite(RELAY_DISPENSE_PIN, LOW);

    snprintf(msg_out, msg_len, "Đã nhả hàng thành công");
    snprintf(result_out, result_len, "{\"status\":\"ok\"}");
    return ESP_OK;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Khởi động máy bán hàng InnoEdge (Arduino) ---");

    pinMode(RELAY_DISPENSE_PIN, OUTPUT);
    digitalWrite(RELAY_DISPENSE_PIN, LOW);

    pinMode(COIN_INPUT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(COIN_INPUT_PIN), onCoinPulse, FALLING);

    // 1. Khởi tạo SDK InnoEdge
    InnoEdge.begin("1.0.0");

    // 2. Đăng ký callback khi khách quét QR thanh toán thành công (on_paid)
    InnoEdge.onPaid([](int64_t intentId, int64_t amountVnd) {
        Serial.printf("[THANH TOÁN QR THÀNH CÔNG] Đơn #%lld: %lld đ -> Kích relay nhả hàng!\n", intentId, amountVnd);
        digitalWrite(RELAY_DISPENSE_PIN, HIGH);
        delay(500);
        digitalWrite(RELAY_DISPENSE_PIN, LOW);
    });

    // 3. Đăng ký nhận thông tin QR động để hiển thị lên màn hình LCD/OLED
    InnoEdge.onQr([](const char *payload, int64_t amountVnd, const char *refCode, int expiresSec, int64_t intentId) {
        Serial.printf("[QR ĐỘNG] Ref: %s - Số tiền: %lld đ (Hết hạn: %d s)\n", refCode, amountVnd, expiresSec);
        Serial.printf("Chuỗi QR để render: %s\n", payload);
    });

    // 4. Đăng ký lệnh từ xa "dispense"
    InnoEdge.onCommand("dispense", handleDispense);

    // 5. Bắt đầu dịch vụ kết nối mạng và cloud (chạy nền)
    InnoEdge.start();
    Serial.printf("Device ID: %s\n", InnoEdge.deviceId());
}

void loop() {
    // Xử lý các xung xu đếm được từ interrupt
    if (pendingCoins > 0) {
        int count = pendingCoins;
        pendingCoins = 0;
        Serial.printf("Khách đút %d xu -> Ghi NVS và gửi Cloud...\n", count);
        InnoEdge.publishCoin(count);
    }

    // In thông tin trạng thái mỗi 5 giây
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 5000) {
        lastCheck = millis();
        Serial.printf("Status: %s | Assigned: %s | Queue NVS: %d\n",
            InnoEdge.isOnline() ? "ONLINE" : "OFFLINE",
            InnoEdge.isAssigned() ? "YES" : "NO",
            InnoEdge.queueDepth());
    }

    delay(10);
}
