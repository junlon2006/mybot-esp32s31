#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/platform_network"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -I"$test_dir/mocks" -I"$repo_root/tests/host_support/include" \
    -I"$repo_root/components/mybot_platform/include" \
    "$repo_root/components/mybot_platform/src/services/network/network_bootstrap.c" \
    "$test_dir/test_network.c" -o "$test_build_dir/test_platform_network"
"$test_build_dir/test_platform_network"
