#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-http-time-sync.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize="${SANITIZERS:-undefined}" \
    -fno-omit-frame-pointer -D_DEFAULT_SOURCE -Dtime=test_time -Dsettimeofday=test_settimeofday \
    -I"$test_dir/mocks" -I"$repo_dir/components/mybot_platform/src/internal" \
    "$test_dir/test_time_sync.c" \
    "$repo_dir/components/mybot_platform/src/services/network/http_time_sync.c" \
    -o "$test_binary"
"$test_binary"
