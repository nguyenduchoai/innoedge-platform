# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge

import os
import time
import logging
import threading

logger = logging.getLogger("innoedge.gpio")

class LinuxGPIO:
    """
    Bộ điều khiển GPIO đa nền tảng cho Raspberry Pi, Banana Pi, Orange Pi.
    Hỗ trợ tự động:
    1. libgpiod (chuẩn Linux hiện đại trên Raspberry Pi OS Bookworm & Armbian)
    2. sysfs (/sys/class/gpio)
    3. Mock GPIO giả lập nếu chạy trên PC / Mac
    """
    def __init__(self, chip_num=0):
        self.chip_num = chip_num
        self.mode = "mock"
        self._gpiod_chip = None

        # 1. Thử libgpiod
        try:
            import gpiod
            self.gpiod = gpiod
            self._gpiod_chip = gpiod.Chip(f"gpiochip{chip_num}")
            self.mode = "libgpiod"
            logger.info(f"[GPIO] Sử dụng libgpiod (chip {chip_num})")
            return
        except Exception:
            pass

        # 2. Thử sysfs
        if os.path.exists("/sys/class/gpio"):
            self.mode = "sysfs"
            logger.info("[GPIO] Sử dụng Linux sysfs (/sys/class/gpio)")
            return

        # 3. Fallback Mock
        logger.info("[GPIO] Chạy ở chế độ Mock GPIO (Mô phỏng phần cứng)")

    def setup_output(self, pin_num, initial=0):
        if self.mode == "libgpiod":
            line = self._gpiod_chip.get_line(pin_num)
            line.request(consumer="innoedge", type=self.gpiod.LINE_REQ_DIR_OUT)
            line.set_value(initial)
            return line
        elif self.mode == "sysfs":
            pdir = f"/sys/class/gpio/gpio{pin_num}"
            if not os.path.exists(pdir):
                try:
                    with open("/sys/class/gpio/export", "w") as f:
                        f.write(str(pin_num))
                except Exception:
                    pass
            time.sleep(0.05)
            try:
                with open(f"{pdir}/direction", "w") as f:
                    f.write("out")
                with open(f"{pdir}/value", "w") as f:
                    f.write(str(initial))
            except Exception as e:
                logger.warning(f"Lỗi sysfs gpio {pin_num}: {e}")
            return pin_num
        else:
            logger.debug(f"[Mock GPIO] Cấu hình chân {pin_num} làm OUTPUT (ban đầu={initial})")
            return pin_num

    def write(self, line_or_pin, value):
        if self.mode == "libgpiod":
            line_or_pin.set_value(1 if value else 0)
        elif self.mode == "sysfs":
            try:
                with open(f"/sys/class/gpio/gpio{line_or_pin}/value", "w") as f:
                    f.write("1" if value else "0")
            except Exception as e:
                logger.warning(f"Lỗi sysfs ghi pin {line_or_pin}: {e}")
        else:
            logger.info(f"[Mock GPIO] Pin {line_or_pin} -> {'HIGH (1)' if value else 'LOW (0)'}")

class PinRelay:
    """
    Module điều khiển Relay nhả tiền / bơm nước / khoá chốt cho Raspberry Pi & Banana Pi.
    Tích hợp trần an toàn phần cứng (Hardware Safety Watchdog).
    """
    def __init__(self, pin=17, active_high=True, max_pulse_sec=10, gpio_controller=None):
        self.pin = pin
        self.active_high = active_high
        self.max_pulse_sec = max_pulse_sec
        self.gpio = gpio_controller or LinuxGPIO()
        
        self.on_val = 1 if active_high else 0
        self.off_val = 0 if active_high else 1
        
        self._handle = self.gpio.setup_output(self.pin, initial=self.off_val)
        self.is_active = False
        self._lock = threading.Lock()

    def on(self):
        with self._lock:
            self.gpio.write(self._handle, self.on_val)
            self.is_active = True
            logger.info(f"[Relay Pin {self.pin}] BẬT")

    def off(self):
        with self._lock:
            self.gpio.write(self._handle, self.off_val)
            self.is_active = False
            logger.info(f"[Relay Pin {self.pin}] TẮT")

    def pulse(self, seconds=1.0):
        """
        Kích hoạt xung Relay chạy nền (không block luồng chính).
        Bảo vệ trần an toàn tối đa: max_pulse_sec.
        """
        if seconds <= 0:
            return
        if seconds > self.max_pulse_sec:
            logger.warning(f"⚠️ Cảnh báo: Thời gian yêu cầu {seconds}s vượt trần {self.max_pulse_sec}s -> Cắt về {self.max_pulse_sec}s!")
            seconds = self.max_pulse_sec

        def _run():
            self.on()
            time.sleep(seconds)
            self.off()

        t = threading.Thread(target=_run, daemon=True)
        t.start()
