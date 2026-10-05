# InnoEdge Global Payment Gateways Connector

[English](README.md) | [Tiếng Việt](README_vi.md)

Multi-Gateway payment adapter enabling InnoEdge devices to accept international payments via **Stripe** (Credit Cards, Apple Pay, Google Pay), **PayPal**, **PromptPay** (Thailand QR), and **VietQR**.

---

## Supported Gateways

| Gateway | Market | Currency | Payment Method | Webhook Route |
|---|---|---|---|---|
| **Stripe** | Global / US / EU | USD, EUR, etc. | Credit/Debit Cards, Apple Pay, Google Pay | `/webhook/stripe` |
| **PayPal** | Global | USD, EUR, etc. | PayPal Wallet, Pay in 4 | `/webhook/paypal` |
| **PromptPay** | Thailand & SE Asia | THB | EMVCo QR Code Banking Transfer | `/webhook/promptpay` |
| **VietQR** | Vietnam | VND | Instant Napas247 Bank Transfer | `/api/webhook/sepay` / `/payos` |

---

## Quick Start

```bash
cd tools/connectors/global-gateways
npm install
npm start
```

The service runs on port `3001` and automatically relays validated payments to InnoEdge devices via the `/api/devices/simulate-paid` endpoint.
