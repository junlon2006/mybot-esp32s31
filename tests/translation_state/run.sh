#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-translation-state.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -I"$test_dir/mocks" -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    "$test_dir/test_state.c" "$sdk_dir/src/core/mybot_state_model.c" -o "$test_binary"
"$test_binary"
