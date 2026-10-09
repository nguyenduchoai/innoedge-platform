# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Trạng thái bền của thiết bị Linux — thay NVS của ESP32.

MỘT file JSON, ghi nguyên tử (file tạm + fsync + rename): mất điện giữa chừng
thì còn bản cũ hoặc bản mới, không bao giờ nửa nọ nửa kia. Gọi dưới khoá của
client — store không tự khoá.
"""

import json
import os
import time
from pathlib import Path

# Tồn tối đa khi mất mạng. Đầy = bỏ khung cũ nhất (mất tiền) → client phát alert.
QUEUE_CAP = 5000


class DurableState:
    def __init__(self, path):
        self.path = Path(path)
        self.recovered_from_corruption = False
        self.data = {
            "next_seq": 0,
            "queue": [],          # khung tiền/sự kiện chờ cloud ack, theo thứ tự seq
            "token": "",
            "assigned": None,
            "last_cmd": 0,        # id lệnh cao nhất từng bắt đầu (dựng lại journal khi hỏng)
            "cmd_journal": None,
            "paid_journal": None,
        }
        self._load()

    def _read(self, path):
        with open(path, "r", encoding="utf-8") as f:
            stored = json.load(f)
        if not isinstance(stored, dict) or not isinstance(stored.get("queue", []), list):
            raise ValueError("cấu trúc sai")
        return stored

    def _load(self):
        # Hai bản cùng nội dung (state.json + state.json.bak): thẻ SD hỏng một sector
        # thì còn bản kia, không mất giao dịch nào.
        bad = []
        for path in (self.path, self._bak):
            if not path.exists():
                continue
            try:
                self.data.update(self._read(path))
                if bad:
                    self.recovered_from_corruption = True
                    self.corruption_reason = f"{', '.join(bad)} hỏng, đã dùng {path.name}"
                return
            except (OSError, ValueError) as e:
                bad.append(f"{path.name} ({e})")
                os.replace(path, path.with_name(f"{path.name}.corrupt-{int(time.time())}"))
        if bad:
            # Mất cả hai: seq đi tiếp từ đồng hồ (ms) để không trùng seq cũ mà cloud
            # đã dedupe — trùng seq = khung tiền mới bị coi là "đã nhận" và bỏ mất.
            self.data["next_seq"] = int(time.time() * 1000)
            self.recovered_from_corruption = True
            self.corruption_reason = f"mất cả hai bản: {'; '.join(bad)} — doi soat lenh/tien gan day"

    @property
    def _bak(self):
        return self.path.with_name(self.path.name + ".bak")

    def _write(self, path, raw):
        tmp = path.with_name(path.name + ".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        raw = json.dumps(self.data, separators=(",", ":"))
        self._write(self.path, raw)
        self._write(self._bak, raw)
        dir_fd = os.open(self.path.parent, os.O_RDONLY)
        try:
            os.fsync(dir_fd)  # rename chỉ bền khi thư mục cũng được fsync
        finally:
            os.close(dir_fd)

    # ── hàng đợi bền ──────────────────────────────────────────────────────────

    def next_seq(self):
        self.data["next_seq"] += 1
        return self.data["next_seq"]

    def enqueue(self, frame):
        """Thêm khung (đã có seq). Trả True nếu phải bỏ khung cũ nhất vì đầy."""
        q = self.data["queue"]
        q.append(frame)
        dropped = len(q) > QUEUE_CAP
        if dropped:
            del q[0]
        return dropped

    def head(self):
        q = self.data["queue"]
        return q[0] if q else None

    def ack(self, seq):
        """Gỡ khung có seq (cloud dedupe theo seq nên ack có thể tới lệch thứ tự)."""
        q = self.data["queue"]
        for i, frame in enumerate(q):
            if frame.get("seq") == seq:
                del q[i]
                return True
        return False

    def depth(self):
        return len(self.data["queue"])
