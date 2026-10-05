# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge

from setuptools import setup, find_packages

setup(
    name="innoedge",
    version="1.0.0",
    description="InnoEdge IoT & Commercial Device SDK for Linux SBCs (Raspberry Pi, Banana Pi, Orange Pi)",
    author="InnoEdge Platform",
    license="Apache-2.0",
    packages=find_packages(),
    python_requires=">=3.7",
    install_requires=[
        # Thư viện thuần chuẩn; gpiod tùy chọn nếu chạy trên bo Pi thật
    ],
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: POSIX :: Linux",
        "Topic :: Home Automation",
        "Topic :: Software Development :: Embedded Systems",
    ],
)
