#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build_dir=$(mktemp -d /tmp/mybot-kv-test.XXXXXX)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$repo_root/tests/kv_store/mocks" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$repo_root/components/mybot_platform/src/services/storage/kv_store.c" \
    "$repo_root/tests/kv_store/test_kv_store.c" -o "$test_build_dir/test_kv_store"
"$test_build_dir/test_kv_store"
