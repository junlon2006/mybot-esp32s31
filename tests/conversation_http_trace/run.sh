#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-http-trace.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize="${SANITIZERS:-undefined}" \
    -fno-omit-frame-pointer \
    -I"$test_dir/mocks" -I"$repo_dir/components/mybot_platform/src/internal" \
    "$test_dir/test_trace.c" \
    "$repo_dir/components/mybot_platform/src/services/network/conversation_http_trace.c" \
    -o "$test_binary"
"$test_binary"
