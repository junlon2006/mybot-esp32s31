#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/korvo_s31_display"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
for source in display/panels/korvo_s31_rgb_panel.c input/korvo_s31_gt1151_touch.c; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
        -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/src/internal" \
        -c "$repo_root/components/mybot_platform/src/drivers/$source" \
        -o "$test_build_dir/$(basename "$source").o"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$test_dir/mocks" -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_display.cc" \
    "$repo_root/components/mybot_platform/src/drivers/display/adapters/lvgl/korvo_s31_lvgl_adapter.cc" \
    "$test_build_dir/korvo_s31_rgb_panel.c.o" "$test_build_dir/korvo_s31_gt1151_touch.c.o" \
    -o "$test_build_dir/test_display"
for scenario in cache_alignment fb0 fb1 panel_new panel_reset panel_init touch_io_new touch_new \
    register_callback port_init port_lock display_new view_new input_new timer_new \
    normal timeout draw_error stop_retry touch_read refcount cleanup_touch cleanup_port; do
    "$test_build_dir/test_display" "$scenario"
done
