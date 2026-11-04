#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    -c "$repo_root/components/mybot_stack/mybot_sdk/mybot/src/core/mybot_ui.c" \
    -o "$test_build_dir/mybot_ui.o"
for language in 0 1; do
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
        -DCONFIG_MYBOT_LANGUAGE_ZH_CN="$language" \
        -I"$repo_root/tests/translation_ui/mocks" \
        -I"$repo_root/components/mybot_platform/src/internal" \
        -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
        "$repo_root/tests/translation_ui/test_view.cc" \
        "$test_build_dir/mybot_ui.o" \
        "$repo_root/components/mybot_platform/src/drivers/display/renderers/lvgl/lvgl_view.cc" \
        -o "$test_build_dir/test_view"
    "$test_build_dir/test_view"
done
python3 "$repo_root/tests/translation_ui/test_font.py"
