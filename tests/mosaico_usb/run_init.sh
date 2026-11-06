#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/mosaico_usb"
board_dir="$repo_root/components/mybot_platform/boards/esp-mosaico-v1.2"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$test_dir/init_mocks" -I"$board_dir" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$test_dir/test_init.c" "$board_dir/mosaico_usb_console.c" \
    -o "$test_build_dir/test_init"
for scenario in descriptors controller cdc_class cdc_uninstall console timer_create timer_start normal; do
    "$test_build_dir/test_init" "$scenario"
done
