#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
cjson="$root/managed_components/espressif__cjson/cJSON"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM
"${CC:-cc}" -c "$cjson/cJSON.c" -o "$work/cjson.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$test_dir/mocks" -I"$cjson" \
    -I"$root/components/esp-wifi-connect/include" "$test_dir/test_portal_json.cc" \
    "$work/cjson.o" -o "$work/test_json"
"$work/test_json"
