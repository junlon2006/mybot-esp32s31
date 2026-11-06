#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build_dir=$(mktemp -d /tmp/mybot-usb-audio.XXXXXX)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Werror ${TEST_SANITIZE_FLAGS:-} \
    -I"$repo_root/tests/mosaico_usb_audio/mocks" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
    "$repo_root/components/mybot_platform/src/drivers/usb/mosaico_usb_audio.c" \
    "$repo_root/components/mybot_platform/src/drivers/usb/mosaico_usb_resampler.c" \
    "$repo_root/tests/mosaico_usb_audio/test_audio.c" -lm -o "$test_build_dir/test_audio"
"$test_build_dir/test_audio" normal
"$test_build_dir/test_audio" failed_prepare
"$test_build_dir/test_audio" capture
"$test_build_dir/test_audio" concurrency
"$test_build_dir/test_audio" resampler
"$test_build_dir/test_audio" playback
"$test_build_dir/test_audio" playback_concurrency
"$test_build_dir/test_audio" playback_drift
"$test_build_dir/test_audio" interpolator
