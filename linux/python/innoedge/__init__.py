# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# InnoEdge Python SDK for Linux Single Board Computers (Raspberry Pi, Banana Pi, Orange Pi)

"""
InnoEdge Linux & SBC Python SDK
Hỗ trợ Raspberry Pi, Banana Pi, Orange Pi, Linux x86/ARM64.
"""

from .client import InnoEdge
from .gpio_pi import PinRelay, LinuxGPIO

__all__ = ["InnoEdge", "PinRelay", "LinuxGPIO"]
__version__ = "1.0.0"
