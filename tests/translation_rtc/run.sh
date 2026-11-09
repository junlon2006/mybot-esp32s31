#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-translation-rtc.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
case "$(uname -s)" in
    Darwin) strip_flag=-Wl,-dead_strip ;;
    *) strip_flag=-Wl,--gc-sections ;;
esac
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wno-unused-function \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections "$strip_flag" \
    -I"$test_dir/mocks" -I"$repo_dir/tests/host_support/include" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    -I"$repo_dir/components/mybot_stack/agora_rtc/include" \
    "$test_dir/test_rtc.c" -o "$test_binary"
"$test_binary"
