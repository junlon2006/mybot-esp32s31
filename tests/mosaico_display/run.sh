#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/mosaico_display"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_display.cc" \
    "$repo_root/components/mybot_platform/src/drivers/display/adapters/lvgl/mosaico_lvgl_adapter.cc" \
    -o "$test_build_dir/test_display"
for scenario in panel_new touch_new register_callback draw_buffer port_init port_lock display_new \
    view_new input_new timer_new normal timeout draw_error stop_retry touch_read refcount \
    cleanup_touch cleanup_port; do
    "$test_build_dir/test_display" "$scenario"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/touch_mocks" -I"$repo_root/components/mybot_platform/src/internal" \
    "$test_dir/test_touch.c" \
    "$repo_root/components/mybot_platform/src/drivers/input/mosaico_cst9220_touch.c" \
    -o "$test_build_dir/test_touch"
"$test_build_dir/test_touch"
