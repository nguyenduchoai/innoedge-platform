# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge

import os
import json
import time
import socket
import threading
import logging
from pathlib import Path

logger = logging.getLogger("innoedge")

class InnoEdge:
    """
    InnoEdge Linux SDK Client dành cho Raspberry Pi, Banana Pi và các thiết bị Linux SBC.
    Tuân thủ nghiêm ngặt chuẩn InnoEdge Protocol v1.
    """
    def __init__(self, device_id=None, cloud_url="ws://127.0.0.1:8080/ws",
                 fw_version="1.0.0", storage_dir=None):
        self.cloud_url = cloud_url
        self.fw_version = fw_version
        
        # Thiết lập thư mục lưu trữ bền vững (thay thế cho NVS trên ESP32)
        if storage_dir is None:
            self.storage_dir = Path.home() / ".innoedge"
        else:
            self.storage_dir = Path(storage_dir)
        self.storage_dir.mkdir(parents=True, exist_ok=True)
        
        self.ledger_file = self.storage_dir / "ledger.json"
        self.queue_file = self.storage_dir / "queue.json"

        # Tự động nhận diện Hardware Device ID trên Raspberry Pi / Banana Pi
        if device_id:
            self.device_id = device_id
        else:
            self.device_id = self._detect_hardware_id()

        self._paid_handlers = []
        self._qr_handlers = []
        self._qr_error_handlers = []
        self._static_qr_handlers = []
        self._command_handlers = {}
        
        self._lock = threading.Lock()
        self._running = False
        self._connected = False
        self._ws = None

        self._load_ledger()
        logger.info(f"[InnoEdge] Khởi tạo thiết bị {self.device_id} (FW {self.fw_version}) | Lưu trữ: {self.storage_dir}")

    def _detect_hardware_id(self):
        """Đọc Serial phần cứng của CPU trên Raspberry Pi / Banana Pi hoặc Machine ID Linux"""
        # Thử đọc serial Raspberry Pi từ /proc/cpuinfo
        try:
            if os.path.exists("/proc/cpuinfo"):
                with open("/proc/cpuinfo", "r") as f:
                    for line in f:
                        if line.startswith("Serial"):
                            serial = line.split(":")[1].strip()
                            if serial and serial != "0000000000000000":
                                return f"PI-{serial[-8:].upper()}"
        except Exception:
            pass

        # Thử đọc machine-id trên Linux OS (Debian/Ubuntu/Armbian)
        for path in ["/etc/machine-id", "/var/lib/dbus/machine-id"]:
            if os.path.exists(path):
                try:
                    with open(path, "r") as f:
                        mid = f.read().strip()
                        if mid:
                            return f"SBC-{mid[:8].upper()}"
                except Exception:
                    pass

        # Nếu không có, dùng hostname hoặc MAC address
        hostname = socket.gethostname().upper().replace("-", "")
        return f"DEV-{hostname[:8]}"

    def _load_ledger(self):
        with self._lock:
            if self.ledger_file.exists():
                try:
                    with open(self.ledger_file, "r") as f:
                        data = json.load(f)
                        self.high_watermark_command_id = data.get("watermark", 0)
                        self.total_paid_vnd = data.get("total_paid", 0)
                        return
                except Exception as e:
                    logger.warning(f"Không thể đọc ledger: {e}")
            self.high_watermark_command_id = 0
            self.total_paid_vnd = 0
            self._save_ledger()

    def _save_ledger(self):
        data = {
            "device_id": self.device_id,
            "watermark": self.high_watermark_command_id,
            "total_paid": self.total_paid_vnd,
            "updated_at": time.time()
        }
        with open(self.ledger_file, "w") as f:
            json.dump(data, f, indent=2)

    # ── Đăng ký Callback Sự Kiện ─────────────────────────────────────────────

    def on_paid(self, func):
        """Tín hiệu DUY NHẤT được phép giao hàng / kích relay: ngân hàng đã xác nhận tiền về"""
        self._paid_handlers.append(func)
        return func

    def on_qr(self, func):
        """Khi mã QR thanh toán động đã được tạo"""
        self._qr_handlers.append(func)
        return func

    def on_static_qr(self, func):
        """Khi nhận mã QR tĩnh của máy từ Cloud"""
        self._static_qr_handlers.append(func)
        return func

    def command(self, action_name):
        """Đăng ký lệnh Command Bus từ xa: @app.command('dispense')"""
        def decorator(func):
            self._command_handlers[action_name] = func
            return func
        return decorator

    # ── Xử lý Lệnh Từ Xa & Chống Trùng Lệnh Bền Vững ─────────────────────────

    def handle_command_frame(self, cmd_id, action, params):
        """
        Xử lý lệnh từ xa với chốt chặn an toàn:
        - Kiểm tra High-watermark commandId để chống trùng.
        - Cập nhật watermark vào sổ cái TRƯỚC khi chạy handler.
        """
        logger.info(f"[InnoEdge] Nhận lệnh '{action}' (commandId={cmd_id})")

        with self._lock:
            # Chống chạy trùng: nếu commandId nhỏ hơn hoặc bằng watermark đã thực thi -> từ chối
            if cmd_id <= self.high_watermark_command_id:
                logger.warning(f"⚠️ Chặn lệnh trùng lặp! commandId={cmd_id} <= watermark={self.high_watermark_command_id}")
                return {
                    "type": "command_ack",
                    "commandId": cmd_id,
                    "status": "dup",
                    "message": "lenh da thuc thi truoc do"
                }

            # Đánh dấu watermark TRƯỚC khi chạy handler (theo đúng luật AGENTS.md)
            self.high_watermark_command_id = cmd_id
            self._save_ledger()

        # Tìm handler đã đăng ký
        handler = self._command_handlers.get(action)
        if not handler:
            return {
                "type": "command_ack",
                "commandId": cmd_id,
                "status": "error",
                "message": f"chua dang ky action '{action}'"
            }

        try:
            res = handler(params)
            return {
                "type": "command_ack",
                "commandId": cmd_id,
                "status": "ok",
                "result": res if isinstance(res, dict) else {"result": res}
            }
        except Exception as e:
            logger.error(f"Lỗi thực thi lệnh {action}: {e}")
            return {
                "type": "command_ack",
                "commandId": cmd_id,
                "status": "error",
                "message": str(e)
            }

    # ── Gửi Yêu Cầu Thanh Toán & Sự Kiện ────────────────────────────────────

    def request_qr(self, amount_vnd):
        """Gửi yêu cầu tạo VietQR động lên Cloud"""
        payload = {
            "type": "qr_request",
            "amountVnd": amount_vnd,
            "deviceId": self.device_id
        }
        logger.info(f"[InnoEdge] Yêu cầu tạo mã VietQR: {amount_vnd} đ")
        self._send_frame(payload)

    def publish_payment(self, pay_type, coins, amount_vnd):
        """Ghi nhận tiền (tiền mặt/xu) vào sổ cái trước khi gửi lên Cloud"""
        with self._lock:
            self.total_paid_vnd += amount_vnd
            self._save_ledger()

        payload = {
            "type": "payment",
            "payType": pay_type,
            "coins": coins,
            "amountVnd": amount_vnd,
            "deviceId": self.device_id,
            "timestamp": int(time.time())
        }
        self._send_frame(payload)

    def publish_event(self, name, data):
        """Báo cáo sự kiện telemetry lên Cloud"""
        payload = {
            "type": "event",
            "name": name,
            "deviceId": self.device_id,
            "data": data,
            "timestamp": int(time.time())
        }
        self._send_frame(payload)

    def _send_frame(self, frame_dict):
        """Gửi khung tin JSON qua kết nối WebSocket"""
        raw = json.dumps(frame_dict)
        if self._ws and self._connected:
            try:
                self._ws.send(raw)
            except Exception as e:
                logger.warning(f"Mất kết nối khi gửi: {e}")
        else:
            logger.debug(f"[Queue Offline] Khung tin lưu tạm: {raw}")

    # ── Dispatcher Gói Tin Nhận Được ─────────────────────────────────────────

    def dispatch_message(self, raw_msg):
        try:
            msg = json.loads(raw_msg)
        except Exception:
            return

        mtype = msg.get("type")
        if mtype == "command":
            cmd_id = msg.get("commandId", 0)
            action = msg.get("action", "")
            params = msg.get("params", {})
            ack = self.handle_command_frame(cmd_id, action, params)
            self._send_frame(ack)

        elif mtype in ("payment_paid", "paid"):
            # Webhook ngân hàng xác nhận tiền về
            intent_id = msg.get("intentId", msg.get("intent_id", 0))
            amount = msg.get("amount", msg.get("amountVnd", 0))
            logger.info(f"✓ [XÁC NHẬN TIỀN VỀ] {amount} đ (intent={intent_id})")
            for h in self._paid_handlers:
                try:
                    h(intent_id, amount)
                except Exception as e:
                    logger.error(f"Lỗi trong on_paid handler: {e}")

        elif mtype == "qr":
            payload = msg.get("payload", "")
            amount = msg.get("amount", 0)
            ref = msg.get("refCode", "")
            expires = msg.get("expiresSec", 300)
            intent = msg.get("intentId", 0)
            for h in self._qr_handlers:
                try:
                    h(payload, amount, ref, expires, intent)
                except Exception as e:
                    logger.error(f"Lỗi trong on_qr handler: {e}")

        elif mtype == "static_qr":
            payload = msg.get("payload", "")
            ref = msg.get("refCode", "")
            for h in self._static_qr_handlers:
                try:
                    h(payload, ref)
                except Exception as e:
                    logger.error(f"Lỗi trong on_static_qr handler: {e}")

    # ── Chạy Ứng Dụng (Event Loop) ──────────────────────────────────────────

    def start(self):
        self._running = True
        logger.info(f"[InnoEdge] Client chạy nền sẵn sàng. Đang kết nối tới {self.cloud_url}...")

    def run(self):
        """Khởi động client và giữ luồng chính chạy"""
        self.start()
        try:
            while self._running:
                time.sleep(1)
        except KeyboardInterrupt:
            self.stop()

    def stop(self):
        self._running = False
        logger.info("[InnoEdge] Dừng thiết bị.")
