#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/wifi_manager"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    ${TEST_SANITIZE_FLAGS:--fsanitize=undefined} \
    -I"$test_dir/mocks" \
    -I"${WIFI_MANAGER_INCLUDE_DIR:-$repo_root/components/esp-wifi-connect/include}" \
    -I"$repo_root/components/esp-wifi-connect/include" \
    "${WIFI_MANAGER_SOURCE:-$repo_root/components/esp-wifi-connect/wifi_manager.cc}" \
    "$test_dir/test_manager.cc" -o "$test_build_dir/test_wifi_manager"
"$test_build_dir/test_wifi_manager"
