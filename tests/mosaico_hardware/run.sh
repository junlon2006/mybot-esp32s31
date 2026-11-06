#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/mosaico_hardware"
board_dir="$repo_root/components/mybot_platform/boards/esp-mosaico-v1.2"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$test_dir/mocks" -I"$board_dir" \
    "$test_dir/test_hardware.c" "$board_dir/mosaico_hardware.c" \
    -o "$test_build_dir/test_hardware"
for scenario in revision_10 efuse revision_11 blank unknown adopt_on adopt_off fade_shared fade_shared_failure \
    gpio_vcc gpio_shutdown gpio_pa gpio_config hold timer channel fade_install fade_set fade_start \
    stop restore i2c normal; do
    "$test_build_dir/test_hardware" "$scenario"
done
