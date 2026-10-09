# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Nhật ký từng-id chống chạy hai lần — cùng ngữ nghĩa với ie_command_journal.h.

Dùng cho lệnh có side-effect (commandId) và tiền QR đã trả (intentId).

High-watermark một số coi mọi id <= max là "đã chạy" nên lệnh tới lệch thứ tự bị
bỏ mà vẫn ack ok. Journal nhớ trạng thái TỪNG id trong 64 id gần nhất:
  RUNNING  ghi TRƯỚC khi chạm phần cứng; còn RUNNING sau khởi động lại = không
           chắc đã chạy hay chưa → đối soát, không tự chạy lại.
  OK/FAILED ghi TRƯỚC khi ack.
Id rơi khỏi cửa sổ (<= retired_through) là "không chắc", không bao giờ thành ok.
"""

UNSEEN, RUNNING, OK, FAILED, TOO_OLD = 0, 1, 2, 3, 4
SLOTS = 64


class Journal:
    def __init__(self, retired_through=0):
        self.retired_through = max(0, int(retired_through))
        self.next = 0
        self.entries = [[0, UNSEEN] for _ in range(SLOTS)]

    def lookup(self, id_):
        if id_ <= 0:
            return TOO_OLD
        for eid, state in self.entries:
            if eid == id_:
                return state
        return TOO_OLD if id_ <= self.retired_through else UNSEEN

    def begin(self, id_):
        """Chiếm slot kế tiếp cho id ở trạng thái RUNNING; trả về slot."""
        slot = self.next
        evicted = self.entries[slot][0]
        if evicted > self.retired_through:
            self.retired_through = evicted
        self.entries[slot] = [id_, RUNNING]
        self.next = (slot + 1) % SLOTS
        return slot

    def interrupted(self):
        return [eid for eid, state in self.entries if state == RUNNING]

    def to_dict(self):
        return {"retired_through": self.retired_through, "next": self.next,
                "entries": [list(e) for e in self.entries]}

    @classmethod
    def from_dict(cls, data, legacy_watermark=0):
        """Dựng lại từ đĩa. Dữ liệu sai cấu trúc → ValueError (caller quyết định)."""
        j = cls()
        entries = data["entries"]
        if not isinstance(entries, list) or len(entries) != SLOTS:
            raise ValueError("journal: sai số slot")
        nxt = int(data["next"])
        if not 0 <= nxt < SLOTS:
            raise ValueError("journal: next ngoài phạm vi")
        parsed = []
        for e in entries:
            eid, state = int(e[0]), int(e[1])
            if eid < 0 or state not in (UNSEEN, RUNNING, OK, FAILED) or (eid == 0) != (state == UNSEEN):
                raise ValueError("journal: slot hỏng")
            parsed.append([eid, state])
        j.entries, j.next = parsed, nxt
        j.retired_through = max(int(data["retired_through"]), 0)
        # Watermark chỉ được nâng ngưỡng khi nó vượt mọi id journal biết (tức có
        # chương trình khác đã chạy lệnh). Bình thường nó <= max id, và nâng lúc đó
        # sẽ bỏ nhầm lệnh tới lệch thứ tự sau khi khởi động lại.
        if int(legacy_watermark) > j.max_id():
            j.retired_through = int(legacy_watermark)
        return j

    def max_id(self):
        return max([self.retired_through] + [eid for eid, _ in self.entries])
