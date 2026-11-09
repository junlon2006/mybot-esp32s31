#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
test_build=$(mktemp -d "${TMPDIR:-/tmp}/mybot-translation-captions.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
# Keep the real shared parser in the test; Apple deprecates its inherited sprintf.
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize="${SANITIZERS:-undefined}" \
    -fno-omit-frame-pointer -Wno-deprecated-declarations \
    -I"$test_dir/mocks" -I"$repo_dir/tests/host_support/include" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    -c "$sdk_dir/src/support/mybot_json.c" -o "$test_build/json.o"
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize="${SANITIZERS:-undefined}" \
    -fno-omit-frame-pointer \
    -I"$test_dir/mocks" -I"$repo_dir/tests/host_support/include" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    "$test_dir/test_captions.c" "$sdk_dir/src/core/mybot_captions.c" \
    "$test_build/json.o" -lm -o "$test_build/test-captions"
"$test_build/test-captions"
