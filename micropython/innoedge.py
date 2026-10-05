# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# InnoEdge MicroPython SDK & InnoBot HAL
# Thư viện MicroPython chính thức dành cho học sinh, sinh viên và Maker STEM.

import time
import json

try:
    import machine
    from machine import Pin, PWM, time_pulse_us
except ImportError:
    # Cho phép chạy kiểm thử syntax trên máy tính không có phần cứng ESP32
    class DummyMachine:
        Pin = None
        PWM = None
    machine = DummyMachine()
    time_pulse_us = None

class InnoBot:
    """
    Lớp điều khiển Robot STEM kết hợp hạ tầng thanh toán thương mại InnoEdge.
    """
    def __init__(self, version="1.0.0", left_motor_pins=(4, 5), right_motor_pins=(6, 7),
                 trig_pin=15, echo_pin=16, servo_pin=18):
        self.version = version
        self.paid_callbacks = []
        self.qr_callbacks = []
        self.command_handlers = {}
        
        # Cấu hình chân phần cứng nếu đang chạy trên bo mạch ESP32
        self.has_hardware = (machine.Pin is not None)
        if self.has_hardware:
            try:
                self.pin_l1 = Pin(left_motor_pins[0], Pin.OUT)
                self.pin_l2 = Pin(left_motor_pins[1], Pin.OUT)
                self.pin_r1 = Pin(right_motor_pins[0], Pin.OUT)
                self.pin_r2 = Pin(right_motor_pins[1], Pin.OUT)
                self.stop()

                self.trig = Pin(trig_pin, Pin.OUT)
                self.echo = Pin(echo_pin, Pin.IN)
                self.trig.value(0)

                self.servo_pwm = PWM(Pin(servo_pin), freq=50)
                self.set_servo(1, 0) # Ban đầu đóng nắp thùng đồ
            except Exception as e:
                print(f"[InnoBot] Cảnh báo khởi tạo phần cứng: {e}")
                self.has_hardware = False
        else:
            print("[InnoBot] Đang chạy ở chế độ mô phỏng giả lập (Simulation Mode)")

    # ── Động cơ & Chuyển động ────────────────────────────────────────────────

    def forward(self, speed=80, seconds=1.0):
        print(f"[InnoBot] ⬆️ TIẾN LÊN: tốc độ {speed}% trong {seconds}s")
        if self.has_hardware:
            self.pin_l1.value(1); self.pin_l2.value(0)
            self.pin_r1.value(1); self.pin_r2.value(0)
        time.sleep(seconds)
        self.stop()

    def backward(self, speed=80, seconds=1.0):
        print(f"[InnoBot] ⬇️ LÙI LẠI: tốc độ {speed}% trong {seconds}s")
        if self.has_hardware:
            self.pin_l1.value(0); self.pin_l2.value(1)
            self.pin_r1.value(0); self.pin_r2.value(1)
        time.sleep(seconds)
        self.stop()

    def turn_left(self, speed=80, seconds=0.5):
        print(f"[InnoBot] ⬅️ RẼ TRÁI trong {seconds}s")
        if self.has_hardware:
            self.pin_l1.value(0); self.pin_l2.value(1)
            self.pin_r1.value(1); self.pin_r2.value(0)
        time.sleep(seconds)
        self.stop()

    def turn_right(self, speed=80, seconds=0.5):
        print(f"[InnoBot] ➡️ RẼ PHẢI trong {seconds}s")
        if self.has_hardware:
            self.pin_l1.value(1); self.pin_l2.value(0)
            self.pin_r1.value(0); self.pin_r2.value(1)
        time.sleep(seconds)
        self.stop()

    def stop(self):
        if self.has_hardware:
            self.pin_l1.value(0); self.pin_l2.value(0)
            self.pin_r1.value(0); self.pin_r2.value(0)

    # ── Cảm biến siêu âm & Servo ─────────────────────────────────────────────

    def get_distance(self):
        """Đo khoảng cách vật cản bằng cảm biến HC-SR04 (cm)"""
        if not self.has_hardware or time_pulse_us is None:
            return 25.0 # Giá trị giả lập
        
        self.trig.value(0)
        time.sleep_us(2)
        self.trig.value(1)
        time.sleep_us(10)
        self.trig.value(0)
        
        duration = time_pulse_us(self.echo, 1, 30000) # timeout 30ms (~5m)
        if duration < 0:
            return 999.0
        dist = (duration * 0.0343) / 2
        return round(dist, 1)

    def set_servo(self, channel=1, angle=90):
        """Quay góc Servo (0° - 180°)"""
        print(f"[InnoBot] 🦾 SERVO kênh {channel} quay góc {angle}°")
        if self.has_hardware:
            duty = int(26 + (angle / 180.0) * 102) # 50Hz: 0.5ms - 2.5ms duty
            self.servo_pwm.duty(duty)

    def speak(self, text):
        print(f"[InnoBot] 🗣️ ROBOT PHÁT LOA: \"{text}\"")

    # ── Decorator Sự Kiện & Lệnh InnoEdge ────────────────────────────────────

    def on_paid(self, func):
        self.paid_callbacks.append(func)
        return func

    def on_qr(self, func):
        self.qr_callbacks.append(func)
        return func

    def command(self, action_name):
        def decorator(func):
            self.command_handlers[action_name] = func
            return func
        return decorator

    # ── Giả lập & Vòng lặp ──────────────────────────────────────────────────

    def simulate_payment(self, amount_vnd=20000, intent_id=101):
        print(f"[InnoBot] 🏦 Giả lập sự kiện ngân hàng báo tiền về: {amount_vnd} đ (intent={intent_id})")
        for cb in self.paid_callbacks:
            cb(intent_id, amount_vnd)

    def run(self):
        print(f"[InnoBot] Khởi chạy InnoEdge Robot STEM (v{self.version}) thành công.")
        print("[InnoBot] Sẵn sàng nhận lệnh từ xa hoặc sự kiện thanh toán VietQR.")
