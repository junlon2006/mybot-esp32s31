#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-translation-volume.XXXXXX")
trap 'rm -f "$test_binary" "$test_binary.json.o"' EXIT HUP INT TERM
case "$(uname -s)" in
    Darwin) strip_flag=-Wl,-dead_strip ;;
    *) strip_flag=-Wl,--gc-sections ;;
esac
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections \
    -I"$repo_dir/tests/translation_control/mocks" \
    -I"$repo_dir/tests/host_support/include" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    -c "$sdk_dir/src/support/mybot_json.c" -o "$test_binary.json.o"
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wno-unused-function \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections "$strip_flag" \
    -I"$repo_dir/tests/translation_control/mocks" \
    -I"$repo_dir/tests/host_support/include" \
    -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    "$test_dir/test_volume.c" \
    "$sdk_dir/src/core/mybot_captions.c" "$test_binary.json.o" \
    "$sdk_dir/src/media/mybot_audio.c" "$sdk_dir/src/media/mybot_media_pipeline.c" \
    "$sdk_dir/src/core/mybot_presenter.c" "$sdk_dir/src/core/mybot_state_model.c" \
    -lm -o "$test_binary"
"$test_binary"
