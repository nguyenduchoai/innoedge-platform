# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge

from setuptools import setup, find_packages

setup(
    name="innoedge",
    version="0.2.0",
    description="InnoEdge device SDK for Linux (Raspberry Pi, Rockchip, Jetson, x86) - PROTOCOL-v1",
    author="InnoEdge Platform",
    license="Apache-2.0",
    packages=find_packages(),
    python_requires=">=3.8",
    install_requires=[
        "websocket-client>=1.6",  # WebSocket tới cloud; gpiod tuỳ chọn cho GPIO
    ],
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: POSIX :: Linux",
        "Topic :: Home Automation",
        "Topic :: Software Development :: Embedded Systems",
    ],
)
