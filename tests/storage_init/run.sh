#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir=$(mktemp -d /tmp/mybot-storage-init.XXXXXX)
trap 'rm -r -- "$test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$repo_root/tests/storage_init/mocks" -I"$repo_root/main" \
    "$repo_root/main/storage_init.c" "$repo_root/tests/storage_init/test_storage.c" \
    -o "$test_dir/test_storage"
"$test_dir/test_storage"
