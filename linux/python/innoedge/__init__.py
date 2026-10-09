# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""InnoEdge SDK cho Linux (Raspberry Pi, Rockchip RK35xx, Jetson, x86) — PROTOCOL-v1."""

from . import artifact
from .client import SDK_VERSION, InnoEdge
from .gpio_pi import LinuxGPIO, PinRelay

__all__ = ["InnoEdge", "PinRelay", "LinuxGPIO", "artifact"]
__version__ = SDK_VERSION
