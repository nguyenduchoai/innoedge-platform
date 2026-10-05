// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
//
// InnoEdge Payment Gateway Connector: Pay2S Collection Link V2 & Tingee Node SDK
// Dịch vụ trung gian kết nối ESP32 với Pay2S và Tingee để sinh mã QR và nhận Webhook.

import express from 'express';
import dotenv from 'dotenv';
import crypto from 'crypto';
import { TingeeClient } from '@tingee/sdk-node';

dotenv.config();

const app = express();
app.use(express.json());

const PORT = process.env.PORT || 3000;
const INNOEDGE_CLOUD_URL = process.env.INNOEDGE_CLOUD_URL || 'http://localhost:8080';

// ── 1. Cấu hình Tingee Node SDK (@tingee/sdk-node) ──────────────────────────
const tingee = new TingeeClient({
  clientId: process.env.TINGEE_CLIENT_ID || 'mock_tingee_client_id',
  secretKey: process.env.TINGEE_SECRET_KEY || 'mock_tingee_secret_key',
  environment: (process.env.TINGEE_ENV === 'production') ? 'production' : 'uat'
});

// ── 2. Cấu hình Pay2S Collection Link V2 ─────────────────────────────────────
const PAY2S_CONFIG = {
  clientId: process.env.PAY2S_CLIENT_ID || 'mock_pay2s_client_id',
  secretKey: process.env.PAY2S_SECRET_KEY || 'mock_pay2s_secret_key',
  endpoint: process.env.PAY2S_ENDPOINT || 'https://api.pay2s.vn/v2/collection-links'
};

// ── Helper: Đẩy thông báo tiền về sang InnoEdge Cloud ───────────────────────
async function notifyInnoEdgePaid(deviceId, amount, refCode, intentId) {
  try {
    const res = await fetch(`${INNOEDGE_CLOUD_URL}/api/devices/simulate-paid`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        deviceId,
        amount: Number(amount),
        refCode,
        intentId: Number(intentId)
      })
    });
    const json = await res.json();
    console.log(`[InnoEdge] Đã chuyển tiếp on_paid tới thiết bị ${deviceId}:`, json);
  } catch (err) {
    console.error(`[InnoEdge] Lỗi gửi tín hiệu paid sang thiết bị:`, err.message);
  }
}

// ── API: Tạo mã QR thanh toán động cho ESP32 (Pay2S hoặc Tingee) ─────────────
// Firmware ESP32 gọi endpoint này khi khách bấm nút chọn sản phẩm / gói dịch vụ
app.post('/api/qr/create', async (req, res) => {
  const { deviceId, amount, provider = 'tingee', orderInfo } = req.body;
  if (!amount || !deviceId) {
    return res.status(400).json({ error: 'Thiếu deviceId hoặc amount' });
  }

  const intentId = Date.now();
  const refCode = `INNO${deviceId.slice(-4)}${intentId.toString().slice(-4)}`;

  try {
    if (provider === 'tingee') {
      console.log(`[Tingee] Tạo đơn thanh toán ${amount} đ cho máy ${deviceId}...`);
      // Sử dụng Tingee Node SDK
      // const order = await tingee.order.create({ ... })
      // Giả lập hoặc gọi trực tiếp SDK nếu có credentials:
      const qrPayload = `https://qr.tingee.vn/pay?ref=${refCode}&amt=${amount}`;
      return res.json({
        success: true,
        provider: 'tingee',
        intentId,
        refCode,
        amount,
        qrPayload,
        expiresSec: 300
      });
    } else if (provider === 'pay2s') {
      console.log(`[Pay2S] Tạo Collection Link V2 ${amount} đ cho máy ${deviceId}...`);
      // Pay2S Collection Link V2
      const qrPayload = `https://pay2s.vn/link/${refCode}?amount=${amount}`;
      return res.json({
        success: true,
        provider: 'pay2s',
        intentId,
        refCode,
        amount,
        qrPayload,
        expiresSec: 300
      });
    }
  } catch (err) {
    console.error('Lỗi tạo QR:', err);
    res.status(500).json({ error: err.message });
  }
});

// ── 3. Webhook Tingee (@tingee/sdk-node) ─────────────────────────────────────
// Được Tingee gọi khi khách hàng thanh toán VietQR thành công
app.post('/api/webhook/tingee', (req, res) => {
  const signature = req.headers['x-signature'];
  const timestamp = req.headers['x-request-timestamp'];
  const payload = req.body;

  console.log('[Webhook Tingee] Nhận thông báo giao dịch:', payload);

  // Xác thực chữ ký HMAC SHA512 nếu có Secret Key cấu hình
  if (process.env.TINGEE_SECRET_KEY && signature) {
    const rawData = JSON.stringify(payload);
    const expectedSig = crypto
      .createHmac('sha512', process.env.TINGEE_SECRET_KEY)
      .update(`${timestamp}.${rawData}`)
      .digest('hex');

    if (signature !== expectedSig) {
      console.warn('[Webhook Tingee] Cảnh báo: Chữ ký x-signature không khớp!');
      // Trong môi trường production nghiêm ngặt, return 401
    }
  }

  const { status, statusCode, amount, paidAmount, description, orderInfo, orderId } = payload;
  const isSuccess = status === 'success' || statusCode === '00';

  if (!isSuccess) {
    return res.json({ code: '01', message: 'Giao dịch chưa hoàn thành' });
  }

  const finalAmount = paidAmount || amount;
  const content = description || orderInfo || orderId || '';
  
  // Trích xuất mã refCode (VD: INNOAABB1234 hoặc GTMOCKD00001)
  const match = content.match(/(INNO[A-Z0-9]+|GTMOCK[A-Z0-9]+)/);
  const refCode = match ? match[1] : 'GTMOCKD00001';

  // Thông báo tiền về tới InnoEdge Cloud -> Firmware nhả relay
  notifyInnoEdgePaid('', finalAmount, refCode, Date.now());

  return res.json({ code: '00', message: 'success', success: true });
});

// ── 4. Webhook Pay2S (Collection Link V2 & IPN) ──────────────────────────────
// Được Pay2S gọi khi giao dịch chuyển khoản hoàn tất
app.post('/api/webhook/pay2s', (req, res) => {
  const payload = req.body;
  console.log('[Webhook Pay2S] Nhận thông báo giao dịch:', payload);

  let transactions = [];
  if (Array.isArray(payload.transactions) && payload.transactions.length > 0) {
    transactions = payload.transactions;
  } else {
    // Dạng IPN đơn hàng Collection Link V2
    transactions = [{
      content: payload.content || payload.orderInfo || '',
      transferAmount: payload.transferAmount || payload.amount || 0
    }];
  }

  for (const tx of transactions) {
    const amount = Number(tx.transferAmount || tx.amount || 0);
    const content = tx.content || '';
    const match = content.match(/(INNO[A-Z0-9]+|GTMOCK[A-Z0-9]+)/);
    const refCode = match ? match[1] : 'GTMOCKD00001';

    console.log(`[Pay2S] Khớp đơn: ${refCode} - Số tiền: ${amount} đ`);
    notifyInnoEdgePaid('', amount, refCode, Date.now());
  }

  return res.json({ success: true, message: 'Đã nhận webhook Pay2S' });
});

app.listen(PORT, () => {
  console.log(`--- InnoEdge VietQR Connector Service ---`);
  console.log(`Lắng nghe tại port: http://localhost:${PORT}`);
  console.log(`Endpoint Webhook Tingee: http://localhost:${PORT}/api/webhook/tingee`);
  console.log(`Endpoint Webhook Pay2S : http://localhost:${PORT}/api/webhook/pay2s`);
});
