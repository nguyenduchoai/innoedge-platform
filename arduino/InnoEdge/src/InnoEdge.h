// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#pragma once

#ifdef __cplusplus

#include <innoedge.h>
#include <functional>

typedef std::function<void(int64_t intent_id, int64_t amount_vnd)> InnoEdgePaidCallback;
typedef std::function<void(const char *payload, int64_t amount_vnd, const char *ref_code, int expires_sec, int64_t intent_id)> InnoEdgeQrCallback;
typedef std::function<void(const char *method, int coins, int64_t amount_vnd, bool duplicate)> InnoEdgePaymentAckCallback;
typedef std::function<void(int version)> InnoEdgeConfigCallback;
typedef std::function<void()> InnoEdgeSimpleCallback;

class InnoEdgeClass {
public:
    InnoEdgeClass();

    // Khởi tạo SDK với phiên bản firmware.
    esp_err_t begin(const char *fw_version = "1.0.0");

    // Bắt đầu kết nối mạng và cloud (chạy nền, không chặn loop).
    esp_err_t start();

    // Kiểm tra trạng thái
    bool isOnline() const;
    bool isAssigned() const;
    const char *deviceId() const;
    uint32_t queueDepth() const;

    // Gửi thanh toán / sự kiện lên cloud (ghi NVS trước, gửi sau)
    esp_err_t publishPayment(innoedge_payment_kind_t kind, int count, int64_t amount_vnd = 0);
    esp_err_t publishCoin(int count, int64_t amount_vnd = 0);
    esp_err_t publishCash(int64_t amount_vnd);
    esp_err_t publishEvent(const char *name, const char *data_json);

    // Yêu cầu sinh mã QR thanh toán động
    esp_err_t requestQr(int64_t amount_vnd);

    // Đăng ký lệnh từ xa từ cloud (có chống trùng NVS)
    esp_err_t onCommand(const char *action, innoedge_command_fn fn);

    // Gửi cảnh báo vận hành
    esp_err_t alert(const char *code, const char *severity, const char *message, bool active = true);

    // Đăng ký các callback sự kiện
    void onPaid(InnoEdgePaidCallback cb);
    void onQr(InnoEdgeQrCallback cb);
    void onPaymentAck(InnoEdgePaymentAckCallback cb);
    void onConfig(InnoEdgeConfigCallback cb);
    void onAssigned(InnoEdgeSimpleCallback cb);
    void onUnassigned(InnoEdgeSimpleCallback cb);
    void onProvisioning(InnoEdgeSimpleCallback cb);

private:
    innoedge_config_t m_cfg;
    innoedge_events_t m_events;

    InnoEdgePaidCallback m_onPaid;
    InnoEdgeQrCallback m_onQr;
    InnoEdgePaymentAckCallback m_onPaymentAck;
    InnoEdgeConfigCallback m_onConfig;
    InnoEdgeSimpleCallback m_onAssigned;
    InnoEdgeSimpleCallback m_onUnassigned;
    InnoEdgeSimpleCallback m_onProvisioning;

    static void c_on_paid(int64_t intent_id, int64_t amount_vnd);
    static void c_on_qr(const char *payload, int64_t amount_vnd, const char *ref_code, int expires_sec, int64_t intent_id);
    static void c_on_payment_ack(const char *method, int coins, int64_t amount_vnd, int64_t rate_vnd, bool duplicate);
    static void c_on_config(int version);
    static void c_on_assigned();
    static void c_on_unassigned();
    static void c_on_provisioning();

    static InnoEdgeClass *s_instance;
};

extern InnoEdgeClass InnoEdge;

#endif // __cplusplus
