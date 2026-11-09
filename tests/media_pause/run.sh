#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-media-pause.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
for cloud_aec in 0 1; do
"${CC:-cc}" -DMYBOT_CLOUD_AEC="$cloud_aec" -std=c99 -Wall -Wextra -Werror -pthread \
    -fsanitize=undefined -fno-omit-frame-pointer \
    -I"$test_dir/mocks" -I"$repo_dir/tests/translation_control/mocks" \
    -I"$repo_dir/tests/host_support/include" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    "$test_dir/test_pause.c" "$sdk_dir/src/media/mybot_audio.c" \
    "$sdk_dir/src/media/mybot_media_pipeline.c" "$sdk_dir/src/support/mybot_ringbuf.c" \
    -o "$test_binary"
"$test_binary"
done
