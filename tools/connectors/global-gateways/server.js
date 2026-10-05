// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
//
// InnoEdge Global Payment Gateway Connector
// Multi-Gateway Adapter: Stripe, PayPal, PromptPay (Thailand) & VietQR

import express from 'express';
import dotenv from 'dotenv';
import crypto from 'crypto';

dotenv.config();

const app = express();
app.use(express.json());

const PORT = process.env.PORT || 3001;
const INNOEDGE_CLOUD_URL = process.env.INNOEDGE_CLOUD_URL || 'http://localhost:8080';

// ── Helper: Notify InnoEdge Cloud (triggers on_paid event on target hardware) ─
async function notifyInnoEdgePaid(deviceId, amount, refCode, intentId, currency = 'USD') {
  try {
    const res = await fetch(`${INNOEDGE_CLOUD_URL}/api/devices/simulate-paid`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        deviceId,
        amount: Number(amount),
        refCode,
        intentId: Number(intentId),
        currency
      })
    });
    const json = await res.json();
    console.log(`[InnoEdge] Forwarded payment confirmation to device ${deviceId}:`, json);
  } catch (err) {
    console.error(`[InnoEdge] Failed to forward payment signal:`, err.message);
  }
}

// ── PromptPay CRC16 Calculation (CCITT-FALSE) ───────────────────────────────
function calculateCRC16(data) {
  let crc = 0xFFFF;
  for (let i = 0; i < data.length; i++) {
    crc ^= data.charCodeAt(i) << 8;
    for (let j = 0; j < 8; j++) {
      if ((crc & 0x8000) !== 0) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  return (crc & 0xFFFF).toString(16).toUpperCase().padStart(4, '0');
}

// Generate PromptPay EMVCo QR String for Thailand
function generatePromptPayQR(phoneNumberOrId, amount) {
  const sanitized = phoneNumberOrId.replace(/[^0-9]/g, '');
  let target = sanitized;
  let tag = '01'; // 01 for Mobile Phone, 02 for National ID
  if (sanitized.length === 10 && sanitized.startsWith('0')) {
    target = '0066' + sanitized.substring(1);
  } else if (sanitized.length === 13) {
    tag = '02';
  }

  const aid = 'A000000677010111';
  const sub00 = `00${aid.length.toString().padStart(2, '0')}${aid}`;
  const subTarget = `${tag}${target.length.toString().padStart(2, '0')}${target}`;
  const merchantInfo = sub00 + subTarget;

  const amountStr = Number(amount).toFixed(2);
  const amountTag = `54${amountStr.length.toString().padStart(2, '0')}${amountStr}`;

  let raw = `00020101021229${merchantInfo.length.toString().padStart(2, '0')}${merchantInfo}5303764${amountTag}5802TH6304`;
  const checksum = calculateCRC16(raw);
  return raw + checksum;
}

// ── Unified Payment Creation API ────────────────────────────────────────────
// Firmware or Kiosk calls this to request a dynamic checkout session
app.post('/api/pay/create', async (req, res) => {
  const { deviceId, amount, currency = 'USD', provider = 'stripe', itemName = 'IoT Service' } = req.body;
  if (!deviceId || !amount) {
    return res.status(400).json({ error: 'Missing deviceId or amount' });
  }

  const intentId = Date.now();
  const refCode = `IE-${intentId.toString().slice(-6)}`;

  // 1. Stripe PaymentIntent
  if (provider === 'stripe') {
    const paymentUrl = `https://checkout.stripe.com/pay/${intentId}#${refCode}`;
    return res.json({
      success: true,
      provider: 'stripe',
      intentId,
      refCode,
      amount,
      currency,
      qrPayload: paymentUrl,
      paymentUrl,
      expiresSec: 600
    });
  }

  // 2. PayPal Order
  if (provider === 'paypal') {
    const paypalUrl = `https://www.paypal.com/checkoutnow?token=EC-${intentId}`;
    return res.json({
      success: true,
      provider: 'paypal',
      intentId,
      refCode,
      amount,
      currency,
      qrPayload: paypalUrl,
      paymentUrl: paypalUrl,
      expiresSec: 600
    });
  }

  // 3. PromptPay (Thailand QR Standard)
  if (provider === 'promptpay') {
    const promptPayId = process.env.PROMPTPAY_ID || '0812345678';
    const emvcoQR = generatePromptPayQR(promptPayId, amount);
    return res.json({
      success: true,
      provider: 'promptpay',
      intentId,
      refCode,
      amount,
      currency: 'THB',
      qrPayload: emvcoQR,
      expiresSec: 300
    });
  }

  // 4. Default: VietQR (Vietnam EMVCo)
  return res.json({
    success: true,
    provider: 'vietqr',
    intentId,
    refCode,
    amount,
    currency: 'VND',
    qrPayload: `00020101021238570010A00000072701270006970422...${refCode}`,
    expiresSec: 300
  });
});

// ── Webhooks ────────────────────────────────────────────────────────────────

// Stripe Webhook: /webhook/stripe
app.post('/webhook/stripe', async (req, res) => {
  const event = req.body;
  console.log(`[Stripe Webhook] Event received:`, event.type);

  if (event.type === 'payment_intent.succeeded') {
    const paymentIntent = event.data.object;
    const amount = (paymentIntent.amount || 0) / 100;
    const deviceId = paymentIntent.metadata?.deviceId || 'GLOBAL-01';
    const refCode = paymentIntent.metadata?.refCode || paymentIntent.id;
    const intentId = paymentIntent.metadata?.intentId || Date.now();

    await notifyInnoEdgePaid(deviceId, amount, refCode, intentId, paymentIntent.currency?.toUpperCase());
  }

  res.json({ received: true });
});

// PayPal Webhook: /webhook/paypal
app.post('/webhook/paypal', async (req, res) => {
  const event = req.body;
  console.log(`[PayPal Webhook] Event:`, event.event_type);

  if (event.event_type === 'PAYMENT.CAPTURE.COMPLETED') {
    const capture = event.resource;
    const amount = Number(capture.amount?.value || 0);
    const refCode = capture.custom_id || capture.id;
    const deviceId = capture.invoice_id || 'GLOBAL-01';

    await notifyInnoEdgePaid(deviceId, amount, refCode, Date.now(), capture.amount?.currency_code);
  }

  res.json({ status: 'success' });
});

// PromptPay Webhook: /webhook/promptpay
app.post('/webhook/promptpay', async (req, res) => {
  const { transactionId, amount, refCode, deviceId = 'GLOBAL-01' } = req.body;
  console.log(`[PromptPay Webhook] Tx: ${transactionId} Amount: ${amount} THB Ref: ${refCode}`);

  await notifyInnoEdgePaid(deviceId, amount, refCode, Date.now(), 'THB');
  res.json({ status: 'ok' });
});

app.get('/health', (req, res) => {
  res.json({ status: 'ok', service: 'InnoEdge Global Payment Gateways Connector', version: '1.0.0' });
});

app.listen(PORT, () => {
  console.log(`[InnoEdge Global Gateways] Connector running on port ${PORT}`);
  console.log(`  - Stripe Webhook:    http://localhost:${PORT}/webhook/stripe`);
  console.log(`  - PayPal Webhook:    http://localhost:${PORT}/webhook/paypal`);
  console.log(`  - PromptPay Webhook: http://localhost:${PORT}/webhook/promptpay`);
});
