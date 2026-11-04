#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
aosl_dir="$repo_dir/components/mybot_stack/aosl"
test_build=$(mktemp -d "${TMPDIR:-/tmp}/mybot-translation-http.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
case "$(uname -s)" in
    Darwin) strip_flag=-Wl,-dead_strip ;;
    *) strip_flag=-Wl,--gc-sections ;;
esac
# Plain TCP helpers are unreachable in this HTTPS-only build, as in firmware.
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wno-unused-function \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections "$strip_flag" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    -I"$aosl_dir/include" -I"$aosl_dir/platform/include" \
    -I"$aosl_dir/platform/src/linux/config" \
    "$test_dir/test_http.c" "$sdk_dir/src/support/mybot_http_client.c" \
    "$sdk_dir/src/support/mybot_url.c" "$sdk_dir/src/support/mybot_http_header.c" \
    -o "$test_build/test-http"
"$test_build/test-http"
