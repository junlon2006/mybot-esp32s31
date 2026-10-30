#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/korvo_s31_input"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    -c "$repo_root/components/mybot_platform/src/drivers/input/korvo_s31_adc_keys.c" \
    -o "$test_build_dir/keys.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_keys.cc" "$test_build_dir/keys.o" -o "$test_build_dir/test_keys"
for scenario in mutex adc_new partial_adc_new channel calibrate task cleanup_retry behavior ordering \
    touch touch_stale touch_ordering; do
    "$test_build_dir/test_keys" "$scenario"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    -c "$repo_root/components/mybot_platform/boards/esp32-s31-korvo-1/board.c" \
    -o "$test_build_dir/board.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/include" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_board.cc" "$test_build_dir/board.o" -o "$test_build_dir/test_board"
for scenario in normal hardware lcd_init lcd_render keys; do
    "$test_build_dir/test_board" "$scenario"
done
