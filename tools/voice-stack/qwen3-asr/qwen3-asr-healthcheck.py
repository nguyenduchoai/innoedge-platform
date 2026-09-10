#!/usr/bin/env python3
"""Cheap readiness probe; inference warmup is intentionally separate."""

from urllib.request import urlopen


with urlopen("http://127.0.0.1:8000/health", timeout=4) as response:
    if response.status != 200:
        raise SystemExit(f"unexpected health status: {response.status}")
