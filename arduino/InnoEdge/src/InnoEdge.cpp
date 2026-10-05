// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#include "InnoEdge.h"
#include <cstring>

InnoEdgeClass *InnoEdgeClass::s_instance = nullptr;

InnoEdgeClass::InnoEdgeClass()
{
    s_instance = this;
    std::memset(&m_cfg, 0, sizeof(m_cfg));
    std::memset(&m_events, 0, sizeof(m_events));

    m_events.on_paid = c_on_paid;
    m_events.on_qr = c_on_qr;
    m_events.on_payment_ack = c_on_payment_ack;
    m_events.on_config = c_on_config;
    m_events.on_assigned = c_on_assigned;
    m_events.on_unassigned = c_on_unassigned;
    m_events.on_provisioning = c_on_provisioning;

    m_cfg.events = &m_events;
}

esp_err_t InnoEdgeClass::begin(const char *fw_version)
{
    m_cfg.fw_version = fw_version;
    return innoedge_init(&m_cfg);
}

esp_err_t InnoEdgeClass::start()
{
    return innoedge_start();
}

bool InnoEdgeClass::isOnline() const
{
    return innoedge_is_online();
}

bool InnoEdgeClass::isAssigned() const
{
    return innoedge_is_assigned();
}

const char *InnoEdgeClass::deviceId() const
{
    return innoedge_device_id();
}

uint32_t InnoEdgeClass::queueDepth() const
{
    return innoedge_queue_depth();
}

esp_err_t InnoEdgeClass::publishPayment(innoedge_payment_kind_t kind, int count, int64_t amount_vnd)
{
    return innoedge_publish_payment(kind, count, amount_vnd);
}

esp_err_t InnoEdgeClass::publishCoin(int count, int64_t amount_vnd)
{
    return innoedge_publish_payment(INNOEDGE_PAY_COIN, count, amount_vnd);
}

esp_err_t InnoEdgeClass::publishCash(int64_t amount_vnd)
{
    return innoedge_publish_payment(INNOEDGE_PAY_CASH, 1, amount_vnd);
}

esp_err_t InnoEdgeClass::publishEvent(const char *name, const char *data_json)
{
    return innoedge_publish_event(name, data_json);
}

esp_err_t InnoEdgeClass::requestQr(int64_t amount_vnd)
{
    return innoedge_request_qr(amount_vnd);
}

esp_err_t InnoEdgeClass::onCommand(const char *action, innoedge_command_fn fn)
{
    return innoedge_register_command(action, fn);
}

esp_err_t InnoEdgeClass::alert(const char *code, const char *severity, const char *message, bool active)
{
    return innoedge_alert(code, severity, message, active);
}

void InnoEdgeClass::onPaid(InnoEdgePaidCallback cb) { m_onPaid = cb; }
void InnoEdgeClass::onQr(InnoEdgeQrCallback cb) { m_onQr = cb; }
void InnoEdgeClass::onPaymentAck(InnoEdgePaymentAckCallback cb) { m_onPaymentAck = cb; }
void InnoEdgeClass::onConfig(InnoEdgeConfigCallback cb) { m_onConfig = cb; }
void InnoEdgeClass::onAssigned(InnoEdgeSimpleCallback cb) { m_onAssigned = cb; }
void InnoEdgeClass::onUnassigned(InnoEdgeSimpleCallback cb) { m_onUnassigned = cb; }
void InnoEdgeClass::onProvisioning(InnoEdgeSimpleCallback cb) { m_onProvisioning = cb; }

void InnoEdgeClass::c_on_paid(int64_t intent_id, int64_t amount_vnd)
{
    if (s_instance && s_instance->m_onPaid) {
        s_instance->m_onPaid(intent_id, amount_vnd);
    }
}

void InnoEdgeClass::c_on_qr(const char *payload, int64_t amount_vnd, const char *ref_code, int expires_sec, int64_t intent_id)
{
    if (s_instance && s_instance->m_onQr) {
        s_instance->m_onQr(payload, amount_vnd, ref_code, expires_sec, intent_id);
    }
}

void InnoEdgeClass::c_on_payment_ack(const char *method, int coins, int64_t amount_vnd, int64_t rate_vnd, bool duplicate)
{
    (void)rate_vnd;
    if (s_instance && s_instance->m_onPaymentAck) {
        s_instance->m_onPaymentAck(method, coins, amount_vnd, duplicate);
    }
}

void InnoEdgeClass::c_on_config(int version)
{
    if (s_instance && s_instance->m_onConfig) {
        s_instance->m_onConfig(version);
    }
}

void InnoEdgeClass::c_on_assigned()
{
    if (s_instance && s_instance->m_onAssigned) {
        s_instance->m_onAssigned();
    }
}

void InnoEdgeClass::c_on_unassigned()
{
    if (s_instance && s_instance->m_onUnassigned) {
        s_instance->m_onUnassigned();
    }
}

void InnoEdgeClass::c_on_provisioning()
{
    if (s_instance && s_instance->m_onProvisioning) {
        s_instance->m_onProvisioning();
    }
}

InnoEdgeClass InnoEdge;
