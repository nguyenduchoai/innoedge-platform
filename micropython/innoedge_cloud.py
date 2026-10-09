# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# InnoEdge Cloud cho MicroPython (ESP32) — PROTOCOL-v1, chạy được cả trên CPython.
#
# Cùng cam kết với SDK ESP-IDF / Linux:
#   - tiền/sự kiện ghi flash TRƯỚC khi gửi, gửi lại tới khi cloud ack;
#   - lệnh có side-effect chạy tối đa một lần (nhật ký từng commandId);
#   - on_paid không bao giờ hai lần cho cùng intentId.
# Một luồng: gọi poll() thường xuyên (hoặc run() để chặn mãi).

import json
import os
import socket
import time

try:
    import ubinascii as binascii
except ImportError:
    import binascii
try:
    import uhashlib as hashlib
except ImportError:
    import hashlib

_BACKOFF = (1, 2, 4, 8, 16, 30)
_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
_SLOTS = 64
UNSEEN, RUNNING, OK, FAILED, TOO_OLD = 0, 1, 2, 3, 4
_PLATFORM = ("activation_complete", "reboot", "ota_check", "ble_provision", "set_display", "set_volume")


def _ticks():
    return time.time()


def _is_timeout(e):
    # CPython: socket.timeout; MicroPython: OSError(ETIMEDOUT=110/116 hoặc EAGAIN=11).
    return isinstance(e, getattr(socket, "timeout", ())) or (bool(e.args) and e.args[0] in (11, 110, 116))


class _Journal:
    """Nhật ký từng id (64 mục) — xem ie_command_journal.h."""

    def __init__(self, d=None):
        d = d or {}
        self.rt = d.get("rt", 0)
        self.n = d.get("n", 0)
        self.e = [list(x) for x in d.get("e", [])] or [[0, UNSEEN] for _ in range(_SLOTS)]

    def lookup(self, i):
        if i <= 0:
            return TOO_OLD
        for eid, st in self.e:
            if eid == i:
                return st
        return TOO_OLD if i <= self.rt else UNSEEN

    def begin(self, i):
        slot = self.n
        if self.e[slot][0] > self.rt:
            self.rt = self.e[slot][0]
        self.e[slot] = [i, RUNNING]
        self.n = (slot + 1) % _SLOTS
        return slot

    def dump(self):
        return {"rt": self.rt, "n": self.n, "e": self.e}


class _WS:
    """WebSocket client tối giản (RFC 6455): text frame, ping/pong, close."""

    def __init__(self, url, headers):
        proto, rest = url.split("://", 1)
        hostport, _, path = rest.partition("/")
        host, _, port = hostport.partition(":")
        port = int(port) if port else (443 if proto == "wss" else 80)
        addr = socket.getaddrinfo(host, port, 0, socket.SOCK_STREAM)[0][-1]
        s = socket.socket()
        s.settimeout(10)
        s.connect(addr)
        if proto == "wss":
            import ssl
            if hasattr(ssl, "create_default_context"):  # CPython
                s = ssl.create_default_context().wrap_socket(s, server_hostname=host)
            else:  # MicroPython
                s = ssl.wrap_socket(s, server_hostname=host)
        self.s = s
        key = binascii.b2a_base64(os.urandom(16)).strip()
        req = ("GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n%s\r\n") % (
            path, hostport, key.decode(), "".join(h + "\r\n" for h in headers))
        self._write(req.encode())
        head = b""
        while b"\r\n\r\n" not in head:
            c = self._read(1)
            if not c:
                raise OSError("handshake: kết nối đóng")
            head += c
        accept = binascii.b2a_base64(hashlib.sha1(key + _GUID).digest()).strip()
        if b" 101 " not in head.split(b"\r\n")[0] or accept not in head:
            raise OSError("handshake bị từ chối: " + head.split(b"\r\n")[0].decode())

    def _write(self, b):
        (self.s.write if hasattr(self.s, "write") else self.s.sendall)(b)

    def _read(self, n):
        return self.s.read(n) if hasattr(self.s, "read") else self.s.recv(n)

    def _read_exact(self, n):
        buf = b""
        while len(buf) < n:
            c = self._read(n - len(buf))
            if not c:
                raise OSError("mất kết nối")
            buf += c
        return buf

    def send(self, text, opcode=1):
        data = text.encode() if isinstance(text, str) else text
        n = len(data)
        hdr = bytes([0x80 | opcode])
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126, n >> 8, n & 0xFF])
        else:
            hdr += bytes([0x80 | 127]) + n.to_bytes(8, "big")
        mask = os.urandom(4)
        self._write(hdr + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(data)))

    def recv(self, timeout):
        """Trả chuỗi text, hoặc None nếu hết timeout mà chưa có frame."""
        self.s.settimeout(timeout)
        try:
            first = self._read(1)
        except OSError as e:
            if _is_timeout(e):
                return None  # chưa có frame — không phải lỗi
            raise
        if not first:
            raise OSError("mất kết nối")
        self.s.settimeout(10)  # đã có byte đầu: đọc trọn frame, không bỏ dở giữa chừng
        msg, op0 = b"", None
        while True:
            b0 = first[0]
            b1 = self._read_exact(1)[0]
            n = b1 & 0x7F
            if n == 126:
                n = int.from_bytes(self._read_exact(2), "big")
            elif n == 127:
                n = int.from_bytes(self._read_exact(8), "big")
            mask = self._read_exact(4) if b1 & 0x80 else None
            data = self._read_exact(n) if n else b""
            if mask:
                data = bytes(b ^ mask[i & 3] for i, b in enumerate(data))
            op = b0 & 0x0F
            if op == 8:
                raise OSError("cloud đóng kết nối")
            if op == 9:
                self.send(data, 10)  # pong
            elif op in (0, 1, 2):
                op0 = op0 if op == 0 else op
                msg += data
                if b0 & 0x80:
                    return msg.decode() if op0 == 1 else None
            first = self._read_exact(1)

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


class Cloud:
    def __init__(self, url, device_id, token="", fw_version="0.0.0",
                 storage="/innoedge_state.json", heartbeat_sec=30):
        self.url, self.device_id, self.fw = url, device_id, fw_version
        self.path, self.hb = storage, heartbeat_sec
        self.st = {"seq": 0, "q": [], "token": token, "cmd": None, "paid": None}
        self._load()
        self._cmd = _Journal(self.st["cmd"])
        self._paid = _Journal(self.st["paid"])
        self._ws, self._attempt, self._retry_at = None, 0, 0
        self._t0 = self._last_hb = self._last_head = _ticks()
        self._h = {"paid": [], "qr": [], "qr_error": [], "assigned": [], "frame": []}
        self._cmds = {}
        self._pending_alerts = []
        for name, j in (("command", self._cmd), ("paid", self._paid)):
            for i, (eid, s) in enumerate(j.e):
                if s == RUNNING:
                    self.alert(name + "_interrupted", "critical",
                               "Bi ngat giua chung khi tat may - can doi soat")

    # ── lưu trữ bền ─────────────────────────────────────────────────────────
    def _load(self):
        for p in (self.path, self.path + ".tmp"):
            try:
                with open(p) as f:
                    self.st.update(json.load(f))
                return
            except (OSError, ValueError):
                pass

    def _save(self):
        self.st["cmd"], self.st["paid"] = self._cmd.dump(), self._paid.dump()
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.st, f)
        try:
            os.rename(tmp, self.path)
        except OSError:  # FAT không ghi đè khi rename: còn bản .tmp nếu mất điện ở đây
            os.remove(self.path)
            os.rename(tmp, self.path)

    # ── đăng ký ─────────────────────────────────────────────────────────────
    def on_paid(self, f):
        self._h["paid"].append(f)
        return f

    def on_qr(self, f):
        self._h["qr"].append(f)
        return f

    def on_qr_error(self, f):
        self._h["qr_error"].append(f)
        return f

    def on_assigned(self, f):
        self._h["assigned"].append(f)
        return f

    def command(self, action):
        def deco(f):
            self._cmds[action] = f
            return f
        return deco

    def _emit(self, name, *a):
        for f in self._h[name]:
            try:
                f(*a)
            except Exception as e:
                print("[innoedge] lỗi handler", name, e)

    # ── gửi ─────────────────────────────────────────────────────────────────
    def is_online(self):
        return self._ws is not None

    def queue_depth(self):
        return len(self.st["q"])

    def _send(self, frame):
        if not self._ws:
            return False
        try:
            self._ws.send(json.dumps(frame))
            return True
        except OSError:
            self._drop()
            return False

    def _enqueue(self, frame):
        self.st["seq"] += 1
        frame["seq"], frame["ts"] = self.st["seq"], int(time.time())
        self.st["q"].append(frame)
        self._save()  # OSError lan ra: tiền CHƯA được ghi nhận
        if self.st["q"][0] is frame:
            self._send(frame)
        return frame["seq"]

    def publish_payment(self, kind, count=0, amount_vnd=0):
        """kind: coin | ticket | coin_out (đếm count) | cash (amount_vnd)."""
        if kind == "cash" and amount_vnd > 0:
            return self._enqueue({"type": "payment", "method": "cash", "amount": amount_vnd})
        if count > 0 and kind == "coin":
            return self._enqueue({"type": "coin", "coins": count})
        if count > 0 and kind == "ticket":
            return self._enqueue({"type": "ticket", "tickets": count})
        if count > 0 and kind == "coin_out":
            return self._enqueue({"type": "payment", "method": "coin_out", "coins": count})
        raise ValueError("kind/count/amount không hợp lệ")

    def publish_event(self, name, data=None):
        return self._enqueue({"type": "event", "name": name, "data": data or {}})

    def request_qr(self, amount_vnd):
        if not self._ws:
            return False
        self.st["seq"] += 1
        self._save()
        return self._send({"type": "qr_request", "amount": amount_vnd, "seq": self.st["seq"]})

    def alert(self, code, severity="warning", message="", active=True):
        f = ({"type": "alert", "code": code, "severity": severity, "message": message}
             if active else {"type": "alert", "code": code, "active": False})
        if not self._send(f):
            self._pending_alerts.append(f)

    # ── nhận ────────────────────────────────────────────────────────────────
    def dispatch(self, raw):
        m = json.loads(raw)
        t = m.get("type", "")
        if t in ("coin_ack", "event_ack"):
            self.st["q"] = [f for f in self.st["q"] if f["seq"] != m.get("seq")]
            self._save()
            if self.st["q"]:
                self._send(self.st["q"][0])
        elif t == "command" and m.get("action"):
            self._send(self.handle_command(m.get("commandId", 0), m["action"], m.get("params") or {}))
        elif t == "command" or t in _PLATFORM:
            name = m.get("command") if t == "command" else t
            if name == "activation_complete":
                if m.get("authToken"):
                    self.st["token"] = m["authToken"]
                    self._save()
                self._emit("assigned")
        elif t == "qr":
            self._emit("qr", m.get("qrPayload", ""), m.get("amount", 0), m.get("refCode", ""),
                       m.get("expiresSec", 0), m.get("intentId", 0))
        elif t == "qr_error":
            self._emit("qr_error", m.get("message", ""))
        elif t == "payment_paid":
            self._paid_frame(m.get("intentId", 0), m.get("amount", 0))
        elif t != "hello":
            self._emit("frame", t, raw)

    def handle_command(self, cid, action, params):
        ack = {"type": "command_ack", "commandId": cid, "status": "error"}
        prev = self._cmd.lookup(cid) if isinstance(cid, int) and cid > 0 else TOO_OLD
        if prev == OK:
            ack.update(status="ok", message="duplicate")
        elif prev == FAILED:
            ack["message"] = "previous execution failed; reconcile manually"
        elif prev != UNSEEN:
            ack["message"] = "execution uncertain; reconcile manually, do not replay"
        elif action not in self._cmds:
            ack["message"] = "unknown action"
        else:
            slot = self._cmd.begin(cid)
            self._save()  # RUNNING ghi flash TRƯỚC khi chạm phần cứng
            try:
                res = self._cmds[action](params)
                self._cmd.e[slot][1] = OK
                ack["status"] = "ok"
                if res is not None:
                    ack["result"] = res if isinstance(res, dict) else {"value": res}
            except Exception as e:
                self._cmd.e[slot][1] = FAILED
                ack["message"] = str(e) or "error"
            self._save()
        return ack

    def _paid_frame(self, intent, amount):
        if not isinstance(intent, int) or intent <= 0:
            return
        prev = self._paid.lookup(intent)
        if prev == UNSEEN:
            slot = self._paid.begin(intent)
            self._save()
            self._send({"type": "paid_ack", "intentId": intent})
            self._emit("paid", intent, amount)
            self._paid.e[slot][1] = OK
            self._save()
            return
        self._send({"type": "paid_ack", "intentId": intent})
        if prev != OK:  # bị ngắt / quá cũ: không giao, báo đối soát
            self._send({"type": "alert", "code": "paid_uncertain", "severity": "critical",
                        "message": "QR intent %d da tra, khong chac da giao - doi soat" % intent})

    # ── vòng đời ────────────────────────────────────────────────────────────
    def _drop(self):
        if self._ws:
            self._ws.close()
        self._ws = None
        self._retry_at = _ticks() + _BACKOFF[min(self._attempt, len(_BACKOFF) - 1)]
        self._attempt += 1

    def _connect(self):
        h = ["Device-Id: " + self.device_id, "User-Agent: innoedge-micropython/0.2.1"]
        if self.st["token"]:
            h.append("Authorization: Bearer " + self.st["token"])
        try:
            self._ws = _WS(self.url, h)
        except OSError as e:
            print("[innoedge] chưa kết nối được cloud:", e)
            self._drop()
            return
        self._attempt = 0
        self._send({"type": "hello", "transport": "websocket", "firmware": self.fw,
                    "platform": "micropython"})
        alerts, self._pending_alerts = self._pending_alerts, []
        for a in alerts:
            self._send(a)
        if self.st["q"]:
            self._send(self.st["q"][0])

    def poll(self, timeout=0.2):
        """Một nhịp: kết nối nếu cần, nhận tối đa một frame, heartbeat, gửi lại hàng đợi."""
        now = _ticks()
        if not self._ws:
            if now >= self._retry_at:
                self._connect()
            else:
                time.sleep(timeout)
            return
        try:
            raw = self._ws.recv(timeout)
        except OSError as e:
            print("[innoedge] mất kết nối:", e)
            self._drop()
            return
        if raw:
            self.dispatch(raw)
        if now - self._last_head >= 3 and self.st["q"]:
            self._last_head = now
            self._send(self.st["q"][0])
        if now - self._last_hb >= self.hb:
            self._last_hb = now
            self._send({"type": "heartbeat", "fw_version": self.fw, "platform": "micropython",
                        "uptime_sec": int(now - self._t0), "queue_depth": len(self.st["q"])})

    def run(self):
        while True:
            self.poll()
