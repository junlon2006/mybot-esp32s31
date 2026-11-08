#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/mosaico_input"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    -c "$repo_root/components/mybot_platform/src/services/input/ui_input_router.c" \
    -o "$test_build_dir/ui_input_router.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    -c "$repo_root/components/mybot_platform/src/drivers/input/mosaico_gpio_keys.c" \
    -o "$test_build_dir/mosaico_keys.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_keys.cc" "$test_build_dir/mosaico_keys.o" \
    "$test_build_dir/ui_input_router.o" -o "$test_build_dir/test_mosaico_keys"
for scenario in mutex gpio task gpio_behavior touch touch_stale touch_ordering ui_controls ui_commands late_cancel; do
    "$test_build_dir/test_mosaico_keys" "$scenario"
done

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    -c "$repo_root/components/mybot_platform/boards/esp-mosaico-v1.2/board.c" \
    -o "$test_build_dir/mosaico_board.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -DTEST_MOSAICO=1 ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_board.cc" "$test_build_dir/mosaico_board.o" \
    -o "$test_build_dir/test_mosaico_board"
for scenario in normal hardware lcd_init lcd_render keys usb_unavailable usb_console_error usb_no_stream; do
    "$test_build_dir/test_mosaico_board" "$scenario"
done
