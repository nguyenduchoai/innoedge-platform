# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""InnoEdge client cho Linux (Raspberry Pi, RK3576, Jetson, x86) — PROTOCOL-v1.

Cùng cam kết với SDK ESP32:
- tiền/sự kiện ghi xuống đĩa TRƯỚC khi gửi, gửi lại tới khi cloud ack;
- lệnh có side-effect chạy tối đa một lần (nhật ký từng commandId);
- on_paid không bao giờ tới application hai lần cho cùng một intentId.
"""

import json
import logging
import queue
import re
import threading
import time
import uuid
from pathlib import Path

from . import journal as J
from .store import DurableState

logger = logging.getLogger("innoedge")

SDK_VERSION = "0.2.0"
_EVENT_NAME = re.compile(r"^[A-Za-z0-9_.-]+$")
_BACKOFF_SEC = (1, 2, 4, 8, 16, 30)
_PLATFORM = {"activation_complete", "reboot", "ota_check", "ble_provision", "set_display", "set_volume"}
EVENT_DATA_MAX = 4096


def default_device_id():
    """MAC không dấu phân cách, HOA — đúng định dạng Device-Id của giao thức."""
    node = uuid.getnode()
    if node >> 40 & 1:  # bit multicast = uuid tự bịa số ngẫu nhiên, không phải MAC thật
        raise RuntimeError("không đọc được MAC — truyền device_id=... khi khởi tạo")
    return f"{node:012X}"


class InnoEdge:
    def __init__(self, cloud_url="ws://127.0.0.1:8080/ws/", device_id=None, token=None,
                 fw_version="0.0.0", storage_dir=None, heartbeat_sec=30, heartbeat_extra=None):
        self.cloud_url = cloud_url
        self.device_id = device_id or default_device_id()
        self.fw_version = fw_version
        self.heartbeat_sec = max(5, int(heartbeat_sec))
        self.heartbeat_extra = heartbeat_extra  # () -> dict, gửi kèm heartbeat
        root = Path(storage_dir) if storage_dir else Path.home() / ".innoedge"
        self.state = DurableState(root / "state.json")
        if token and not self.state.data["token"]:
            self.state.data["token"] = token  # factory token; cloud cấp token riêng sau

        self._lock = threading.RLock()        # state + journal
        self._send_lock = threading.Lock()
        self._jobs = queue.Queue()            # handler chạy tuần tự, không chặn luồng nhận
        self._ws = None
        self._connected = threading.Event()
        self._stop = threading.Event()
        self._threads = []
        self._started_at = time.time()
        self._alert_state, self._alert_unsent = {}, {}
        self._command_handlers = {}
        self._handlers = {k: [] for k in ("paid", "qr", "qr_error", "static_qr", "assigned",
                                          "unassigned", "platform", "frame", "payment_ack")}

        if self.state.recovered_from_corruption:
            self.alert("state_file_corrupt", "critical",
                       f"File trang thai hong, da lam lai: {self.state.corruption_reason}")
        self._cmd = self._load_journal("cmd_journal", self.state.data["last_cmd"], "command")
        self._paid = self._load_journal("paid_journal", 0, "paid")

    def _load_journal(self, key, legacy, label):
        raw = self.state.data.get(key)
        if raw is None:
            return J.Journal(legacy)
        try:
            j = J.Journal.from_dict(raw, legacy)
        except (KeyError, TypeError, ValueError, IndexError):
            self.alert(f"{label}_journal_reset", "critical",
                       "Nhat ky hong, da dung lai - doi soat cac giao dich gan day")
            return J.Journal(legacy)
        for id_ in j.interrupted():
            logger.error("%s id=%s bị ngắt giữa chừng lần chạy trước", label, id_)
            self.alert(f"{label}_interrupted", "critical",
                       "Bi ngat giua chung khi tat may - can doi soat, khong tu chay lai")
        return j

    # ── đăng ký callback ─────────────────────────────────────────────────────

    def _on(name):
        def register(self, func):
            self._handlers[name].append(func)
            return func
        return register

    on_paid = _on("paid")             # (intent_id, amount_vnd) — tín hiệu DUY NHẤT được giao hàng
    on_qr = _on("qr")                 # (payload, amount, ref_code, expires_sec, intent_id)
    on_qr_error = _on("qr_error")     # (message)
    on_static_qr = _on("static_qr")   # (payload, ref_code)
    on_assigned = _on("assigned")     # ()
    on_unassigned = _on("unassigned")  # ()
    on_platform_command = _on("platform")  # (name, raw_frame) — reboot/ota_check… tuỳ máy
    on_frame = _on("frame")           # (type, raw_frame) — khung SDK không biết
    on_payment_ack = _on("payment_ack")  # (raw_frame)
    del _on

    def command(self, action):
        """@client.command("dispense") — handler(params) -> dict|None; raise = lỗi."""
        def decorator(func):
            self._command_handlers[action] = func
            return func
        return decorator

    def _emit(self, name, *args):
        for h in self._handlers[name]:
            try:
                h(*args)
            except Exception:
                logger.exception("lỗi trong handler %s", name)

    # ── gửi lên cloud ────────────────────────────────────────────────────────

    def is_online(self):
        return self._connected.is_set()

    def is_assigned(self):
        return bool(self.state.data["assigned"])

    def queue_depth(self):
        with self._lock:
            return self.state.depth()

    def _enqueue(self, frame):
        with self._lock:
            frame["seq"] = self.state.next_seq()
            frame["ts"] = int(time.time())
            dropped = self.state.enqueue(frame)
            try:
                self.state.save()
            except OSError:
                # Báo caller "chưa ghi nhận" thì khung KHÔNG được nằm lại trong RAM rồi
                # theo lần save sau mà đi — app thử lại là doanh thu tính hai lần.
                self.state.ack(frame["seq"])
                self.state.data["next_seq"] -= 1
                raise
        if dropped:
            self.alert("payment_queue_overflow_drop", "critical",
                       "Hang doi day - mot giao dich cu nhat da bi bo")
        self._send_head()
        return frame["seq"]

    def publish_payment(self, kind, count=0, amount_vnd=0):
        """kind: coin | ticket | coin_out (đếm `count`) hoặc cash (`amount_vnd`). Trả seq."""
        if kind == "cash":
            if int(amount_vnd) <= 0:
                raise ValueError("cash cần amount_vnd > 0")
            frame = {"type": "payment", "method": "cash", "amount": int(amount_vnd)}
        elif kind in ("coin", "ticket", "coin_out"):
            if int(count) <= 0:
                raise ValueError(f"{kind} cần count > 0")
            frame = {"coin": {"type": "coin", "coins": int(count)},
                     "ticket": {"type": "ticket", "tickets": int(count)},
                     "coin_out": {"type": "payment", "method": "coin_out", "coins": int(count)}}[kind]
        else:
            raise ValueError(f"kind lạ: {kind}")
        return self._enqueue(frame)

    def publish_event(self, name, data=None):
        """Sự kiện tuỳ ý, cùng hàng đợi bền với tiền (cloud trả event_ack). Trả seq."""
        if not isinstance(name, str) or not _EVENT_NAME.match(name):
            raise ValueError("event name chỉ gồm [A-Za-z0-9_.-]")
        data = {} if data is None else data
        if not isinstance(data, dict) or len(json.dumps(data)) > EVENT_DATA_MAX:
            raise ValueError(f"data phải là dict JSON <= {EVENT_DATA_MAX} byte")
        return self._enqueue({"type": "event", "name": name, "data": data})

    def request_qr(self, amount_vnd):
        """Xin QR động; kết quả về on_qr / on_qr_error. False nếu đang offline."""
        if not self.is_online():
            return False
        with self._lock:
            seq = self.state.next_seq()
            self.state.save()
        return self._send({"type": "qr_request", "amount": int(amount_vnd), "seq": seq})

    def alert(self, code, severity="warning", message="", active=True):
        """Có latch theo code: gọi trong vòng lặp cũng không spam. Offline thì gửi sau."""
        if self._alert_state.get(code) == active:
            return
        self._alert_state[code] = active
        frame = ({"type": "alert", "code": code, "severity": severity, "message": message}
                 if active else {"type": "alert", "code": code, "active": False})
        if not self._send(frame):
            self._alert_unsent[code] = frame

    def _send(self, frame):
        with self._send_lock:
            if self._ws is None:
                return False
            try:
                self._ws.send(json.dumps(frame, separators=(",", ":")))
                return True
            except Exception as e:
                logger.warning("gửi lỗi: %s", e)
                return False

    def _send_head(self):
        with self._lock:
            head = self.state.head()
        if head is not None:
            self._send(head)

    def _heartbeat(self):
        frame = {"type": "heartbeat", "fw_version": self.fw_version, "platform": "linux",
                 "uptime_sec": int(time.time() - self._started_at), "queue_depth": self.queue_depth()}
        if self.heartbeat_extra:
            try:
                frame["telemetry"] = self.heartbeat_extra()
            except Exception:
                logger.exception("heartbeat_extra lỗi")
        self._send(frame)

    # ── nhận từ cloud ────────────────────────────────────────────────────────

    def dispatch_message(self, raw):
        try:
            msg = json.loads(raw)
        except ValueError:
            logger.warning("khung không phải JSON")
            return
        t = msg.get("type", "")
        if t in ("event_ack", "coin_ack"):
            with self._lock:
                if self.state.ack(msg.get("seq")):
                    self.state.save()
            self._send_head()  # ack xong đẩy tiếp phần tử kế → xả hàng đợi nhanh
            if t == "coin_ack":
                self._jobs.put(lambda: self._emit("payment_ack", raw))
        elif t == "command" and msg.get("action"):
            cid, action, params = msg.get("commandId", 0), msg["action"], msg.get("params") or {}
            self._jobs.put(lambda: self.handle_command_frame(cid, action, params))
        elif t == "command" or t in _PLATFORM:
            self._platform(msg.get("command") if t == "command" else t, msg, raw)
        elif t == "status":
            if msg.get("state") == "unassigned":
                self._set_assigned(False)
        elif t == "qr":
            args = (msg.get("qrPayload", ""), msg.get("amount", 0), msg.get("refCode", ""),
                    msg.get("expiresSec", 0), msg.get("intentId", 0))
            self._jobs.put(lambda: self._emit("qr", *args))
        elif t == "qr_error":
            self._jobs.put(lambda: self._emit("qr_error", msg.get("message", "")))
        elif t == "payment_paid":
            intent, amount = msg.get("intentId", 0), msg.get("amount", 0)
            self._jobs.put(lambda: self._handle_paid(intent, amount))
        elif t == "set_static_qr":
            self._jobs.put(lambda: self._emit("static_qr", msg.get("payload", ""), msg.get("refCode", "")))
        elif t != "hello":
            self._jobs.put(lambda: self._emit("frame", t, raw))

    def _set_assigned(self, assigned):
        with self._lock:
            self.state.data["assigned"] = assigned
            self.state.save()
        self._jobs.put(lambda: self._emit("assigned" if assigned else "unassigned"))

    def _platform(self, name, msg, raw):
        if name == "activation_complete":
            token = msg.get("authToken")
            if token:
                with self._lock:
                    self.state.data["token"] = token
            self._set_assigned(True)
        elif name:
            # Linux không tự reboot/flash máy chủ: application quyết định.
            self._jobs.put(lambda: self._emit("platform", name, raw))

    def _persist(self, label):
        try:
            self.state.data["cmd_journal"] = self._cmd.to_dict()
            self.state.data["paid_journal"] = self._paid.to_dict()
            self.state.save()
            return True
        except OSError as e:
            logger.error("ghi nhật ký %s lỗi: %s", label, e)
            return False

    def handle_command_frame(self, cid, action, params):
        """Chạy một lệnh động với nhật ký chống trùng; gửi và trả về command_ack."""
        def ack(status, message=None, result=None):
            frame = {"type": "command_ack", "commandId": cid, "status": status}
            if message:
                frame["message"] = message
            if result is not None:
                frame["result"] = result if isinstance(result, dict) else {"value": result}
            self._send(frame)
            return frame

        if not isinstance(cid, int) or cid <= 0:
            return ack("error", "invalid commandId")
        with self._lock:
            prev = self._cmd.lookup(cid)
            if prev == J.OK:
                return ack("ok", "duplicate")
            if prev == J.FAILED:
                return ack("error", "previous execution failed; reconcile manually")
            if prev != J.UNSEEN:
                return ack("error", "execution uncertain; reconcile manually, do not replay")
            handler = self._command_handlers.get(action)
            if handler is None:
                return ack("error", "unknown action")
            slot = self._cmd.begin(cid)
            self.state.data["last_cmd"] = max(self.state.data["last_cmd"], cid)
            if not self._persist("lệnh"):
                self._cmd.entries[slot] = [0, J.UNSEEN]
                self.alert("command_journal_write_failed", "critical", "Khong luu duoc nhat ky lenh")
                return ack("error", "journal write failed; not executed")
        # Chỉ worker chạy lệnh nên slot không bị ai khác chiếm trong lúc handler chạy.
        try:
            result, ok, message = handler(params), True, None
        except Exception as e:
            result, ok, message = None, False, str(e) or type(e).__name__
        with self._lock:
            self._cmd.entries[slot][1] = J.OK if ok else J.FAILED
            if not self._persist("lệnh"):
                self._cmd.entries[slot][1] = J.RUNNING
                ok, message = False, "result not durable; reconcile manually"
                self.alert("command_result_uncertain", "critical", "Khong luu duoc ket qua lenh")
        return ack("ok" if ok else "error", message, result)

    def _handle_paid(self, intent, amount):
        if not isinstance(intent, int) or intent <= 0:
            logger.error("payment_paid thiếu intentId — không giao")
            return
        with self._lock:
            prev = self._paid.lookup(intent)
            duplicate = prev != J.UNSEEN
            if prev not in (J.UNSEEN, J.OK):
                # Bị ngắt giữa chừng hoặc quá cũ: không chắc đã giao → ack (cloud thôi
                # gửi lại), KHÔNG giao, báo critical để đối soát. Gửi thẳng, không latch,
                # để mỗi intent đều hiện ra.
                frame = {"type": "alert", "code": "paid_uncertain", "severity": "critical",
                         "message": f"QR intent {intent} da tra, khong chac da giao - doi soat"}
                if not self._send(frame):
                    self._alert_unsent["paid_uncertain"] = frame
                logger.error("payment_paid intent=%s không chắc đã giao — báo đối soát", intent)
            if not duplicate:
                slot = self._paid.begin(intent)
                if not self._persist("tiền QR"):
                    self._paid.entries[slot] = [0, J.UNSEEN]
                    self.alert("paid_journal_write_failed", "critical", "Khong luu duoc thanh toan QR")
                    return  # không ack → cloud gửi lại sau
        self._send({"type": "paid_ack", "intentId": intent})
        if duplicate:
            logger.warning("payment_paid intent=%s gửi lại — chỉ ack, KHÔNG giao lần hai", intent)
            return
        self._emit("paid", intent, amount)
        with self._lock:
            self._paid.entries[slot][1] = J.OK
            self._persist("tiền QR")

    # ── vòng đời ─────────────────────────────────────────────────────────────

    def _headers(self):
        h = [f"Device-Id: {self.device_id}", f"Client-Id: {self.device_id}",
             f"User-Agent: innoedge-linux/{SDK_VERSION}"]
        token = self.state.data["token"]
        if token:
            h.append(f"Authorization: Bearer {token}")
        return h

    def _ws_loop(self):
        import websocket  # nạp muộn: test logic không cần thư viện mạng

        attempt = 0
        while not self._stop.is_set():
            try:
                ws = websocket.create_connection(self.cloud_url, header=self._headers(), timeout=10)
            except Exception as e:
                delay = _BACKOFF_SEC[min(attempt, len(_BACKOFF_SEC) - 1)]
                attempt += 1
                logger.warning("chưa kết nối được cloud (%s) — thử lại sau %ss", e, delay)
                self._stop.wait(delay)
                continue
            attempt = 0
            ws.settimeout(1.0)
            with self._send_lock:
                self._ws = ws
            self._connected.set()
            logger.info("đã kết nối %s", self.cloud_url)
            self._send({"type": "hello", "transport": "websocket", "firmware": self.fw_version,
                        "platform": "linux", "sdk": SDK_VERSION})
            for code, frame in list(self._alert_unsent.items()):
                if self._send(frame):
                    self._alert_unsent.pop(code, None)
            self._send_head()
            try:
                while not self._stop.is_set():
                    try:
                        msg = ws.recv()
                    except websocket.WebSocketTimeoutException:
                        continue
                    if isinstance(msg, str) and msg:
                        self.dispatch_message(msg)
            except Exception as e:
                logger.warning("mất kết nối cloud: %s", e)
            finally:
                self._connected.clear()
                with self._send_lock:
                    self._ws = None
                try:
                    ws.close()
                except Exception:
                    pass

    def _tick_loop(self):
        n = 0
        while not self._stop.wait(1.0):
            n += 1
            if not self.is_online():
                continue
            if n % 3 == 0:
                self._send_head()  # cloud chưa ack thì gửi lại
            if n % self.heartbeat_sec == 0:
                self._heartbeat()

    def _work_loop(self):
        while not self._stop.is_set():
            try:
                job = self._jobs.get(timeout=0.5)
            except queue.Empty:
                continue
            try:
                job()
            except Exception:
                logger.exception("lỗi khi xử lý khung từ cloud")

    def start(self):
        if self._threads:
            return
        self._stop.clear()
        for target, name in ((self._ws_loop, "ie-ws"), (self._tick_loop, "ie-tick"),
                             (self._work_loop, "ie-work")):
            t = threading.Thread(target=target, name=name, daemon=True)
            t.start()
            self._threads.append(t)
        logger.info("InnoEdge %s · device=%s · fw=%s", SDK_VERSION, self.device_id, self.fw_version)

    def run(self):
        self.start()
        try:
            while not self._stop.wait(1.0):
                pass
        except KeyboardInterrupt:
            pass
        finally:
            self.stop()

    def stop(self):
        self._stop.set()
        for t in self._threads:
            t.join(timeout=3)
        self._threads = []
