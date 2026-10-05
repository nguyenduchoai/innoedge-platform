#!/bin/sh
# Test host cho SDK — không cần ESP-IDF, không cần phần cứng.
set -e
cd "$(dirname "$0")"
C=../components

echo "▸ command bus (registry + chống trùng lệnh)"
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I stubs -I "$C/innoedge/src" \
   test_command_bus.c "$C/innoedge/src/gtek_command_bus.c" \
   -o /tmp/ie_test_command_bus
/tmp/ie_test_command_bus

if command -v c++ >/dev/null 2>&1; then
    echo "▸ arduino C++ wrapper"
    c++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
        -I "$C/innoedge/include" -I stubs \
        ../arduino/InnoEdge/src/InnoEdge.cpp
    echo "PASS — Arduino C++ wrapper"
fi

if command -v python3 >/dev/null 2>&1; then
    echo "▸ micropython robot test"
    python3 ../micropython/example_robot.py >/dev/null
    echo "PASS — MicroPython InnoBot"
    echo "▸ linux SBC python SDK test"
    python3 ../linux/python/tests/test_linux_sdk.py >/dev/null
    echo "PASS — Linux SBC Python SDK (Raspberry Pi & Banana Pi)"
fi

# mock-cloud & agent là Go — bỏ qua nếu máy không có Go, đừng làm hỏng cả bộ test.
if command -v go >/dev/null 2>&1; then
    echo "▸ linux SBC agent build"
    go build -o /tmp/ie_test_agent ../linux/agent/main.go
    echo "PASS — Linux SBC Agent (innoedge-agent)"
    echo "▸ mock-cloud (khung tin đúng spec giao thức)"
    (cd ../tools/mock-cloud && go test ./...)
    echo "▸ mcp (JSON-RPC qua stdio)"
    (cd ../tools/mcp && go test ./...)
else
    echo "▸ mock-cloud: BỎ QUA (chưa cài Go)"
fi
