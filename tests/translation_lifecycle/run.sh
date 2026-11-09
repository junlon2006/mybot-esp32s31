#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/translation_lifecycle"
sdk_dir="$repo_root/components/mybot_stack/mybot_sdk/mybot"
test_build_dir=$(mktemp -d /tmp/mybot-translation-lifecycle.XXXXXX)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -D_POSIX_C_SOURCE=200809L -I"$test_dir/mocks" \
    -I"$repo_root/tests/host_support/include" -I"$sdk_dir/include" \
    -I"$sdk_dir/src/internal" "$sdk_dir/src/core/mybot_device_lifecycle.c" \
    "$sdk_dir/src/core/mybot_ui.c" \
    "$test_dir/test_lifecycle.c" -o "$test_build_dir/test_lifecycle"
python3 -c 'import subprocess,sys; subprocess.run(sys.argv[1:], check=True, timeout=30)' \
    "$test_build_dir/test_lifecycle"
