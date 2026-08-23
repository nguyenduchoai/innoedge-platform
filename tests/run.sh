#!/bin/sh
# Test host cho SDK — không cần ESP-IDF, không cần phần cứng.
set -e
cd "$(dirname "$0")"
C=../components

echo "▸ command bus (registry + chống trùng lệnh)"
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I stubs -I "$C/command_bus" -I "$C/config_store" -I "$C/net" \
   test_command_bus.c "$C/command_bus/gtek_command_bus.c" \
   -o /tmp/ie_test_command_bus
/tmp/ie_test_command_bus

# mock-cloud là Go — bỏ qua nếu máy không có Go, đừng làm hỏng cả bộ test.
if command -v go >/dev/null 2>&1; then
    echo "▸ mock-cloud (khung tin đúng spec giao thức)"
    (cd ../tools/mock-cloud && go test ./...)
else
    echo "▸ mock-cloud: BỎ QUA (chưa cài Go)"
fi
