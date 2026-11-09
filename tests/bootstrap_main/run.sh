#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-bootstrap-main.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror \
    -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -DCONFIG_MYBOT_DEBUG_RESOURCE_MONITOR=0 \
    -DCONFIG_MYBOT_SERVER_BASE='"https://example.invalid/api"' \
    -DCONFIG_MYBOT_SOURCE_LANGUAGE='"zh-CN"' -DCONFIG_MYBOT_TARGET_LANGUAGE='"en-US"' \
    -I"$test_dir/mocks" -I"$repo_dir/tests/host_support/include" \
    -I"$repo_dir/components/mybot_stack/mybot_sdk/mybot/include" \
    -I"$repo_dir/components/mybot_stack/agora_rtc/include" \
    -I"$repo_dir/components/mybot_platform/include" \
    "$repo_dir/main/app_main.c" "$test_dir/test_main.c" -o "$test_binary"
"$test_binary"
