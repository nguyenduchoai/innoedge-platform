#!/bin/sh
# Test host cho SDK — không cần ESP-IDF, không cần phần cứng.
set -e
cd "$(dirname "$0")"
C=../components
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I stubs -I "$C/command_bus" -I "$C/config_store" -I "$C/net" \
   test_command_bus.c "$C/command_bus/gtek_command_bus.c" \
   -o /tmp/ie_test_command_bus
/tmp/ie_test_command_bus
