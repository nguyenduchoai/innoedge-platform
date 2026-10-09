#!/bin/sh
# Test host cho SDK — không cần ESP-IDF, không cần phần cứng.
set -e
cd "$(dirname "$0")"
C=../components

echo "▸ command bus (registry + nhật ký chống trùng lệnh)"
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I stubs -I "$C/innoedge/src" \
   test_command_bus.c "$C/innoedge/src/ie_command_bus.c" \
   -o /tmp/ie_test_command_bus
/tmp/ie_test_command_bus

echo "▸ chống giao hàng QR hai lần (paid guard)"
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I stubs -I "$C/innoedge/src" \
   test_paid_guard.c "$C/innoedge/src/ie_paid_guard.c" \
   -o /tmp/ie_test_paid_guard
/tmp/ie_test_paid_guard

echo "▸ luật OTA (digest, semver, chống hạ cấp)"
cc -std=c11 -Wall -Wextra -Werror -O1 -I "$C/innoedge/src" \
   test_ota_policy.c -o /tmp/ie_test_ota_policy
/tmp/ie_test_ota_policy

echo "▸ industrial protocols (MDB cashless vending + Modbus RTU)"
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I stubs -I "$C/innoedge/include" -I ../components-hw/innoedge_hw/src \
   test_industrial_protocols.c \
   ../components-hw/innoedge_hw/src/innoedge_mdb.c \
   ../components-hw/innoedge_hw/src/innoedge_modbus.c \
   -o /tmp/ie_test_industrial
/tmp/ie_test_industrial

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
    echo "▸ linux SDK (hàng đợi bền, nhật ký lệnh, tiền QR, cài bản phát hành)"
    python3 ../linux/python/tests/test_linux_sdk.py 2>/dev/null
    echo "PASS — Linux SDK"
    echo "▸ example Jumper (cài bundle + dry-run + rollback)"
    python3 ../linux/python/tests/test_jumper_example.py 2>/dev/null
    echo "PASS — example Jumper"
fi

# mock-cloud là Go — bỏ qua nếu máy không có Go, đừng làm hỏng cả bộ test.
if command -v go >/dev/null 2>&1; then
    echo "▸ mock-cloud (khung tin đúng spec giao thức)"
    (cd ../tools/mock-cloud && go test ./...)
    echo "▸ mcp (JSON-RPC qua stdio)"
    (cd ../tools/mcp && go test ./...)
    # Bằng chứng SDK Linux nối được cloud thật — cần websocket-client.
    PY="${INNOEDGE_PYTHON:-python3}"
    if "$PY" -c "import websocket" 2>/dev/null; then
        echo "▸ linux SDK ↔ mock-cloud qua WebSocket thật"
        "$PY" ../linux/python/tests/test_linux_cloud.py 2>/dev/null
        echo "PASS — Linux SDK ↔ mock-cloud"
    else
        echo "▸ linux SDK ↔ mock-cloud: BỎ QUA (pip install websocket-client, hoặc INNOEDGE_PYTHON=<python có nó>)"
    fi
else
    echo "▸ mock-cloud: BỎ QUA (chưa cài Go)"
fi
