#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/codec_audio"
test_build_dir=$(mktemp -d /tmp/mybot-s31-audio.XXXXXX)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
for local_aec in 0 1; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=undefined \
     -D_POSIX_C_SOURCE=200809L -DCONFIG_MYBOT_DEBUG_RESOURCE_MONITOR=0 \
     -DMYBOT_LOCAL_AEC="$local_aec" \
     -I"$test_dir/mocks" -I"$repo_root/tests/host_support/include" \
     -I"$repo_root/components/mybot_platform/boards/esp-mosaico-v1.2" \
     -I"$repo_root/tests/pcm_playback_buffer/mocks" -I"$repo_root/tests/pcm_playback_buffer" \
     -I"$repo_root/components/mybot_platform/src/internal" \
     -I"$repo_root/components/mybot_stack/mybot_sdk/mybot/include" \
     -I"$repo_root/components/esp_codec_dev/include" -I"$repo_root/components/esp_codec_dev/interface" \
     -I"$repo_root/components/esp_codec_dev/device/include" \
     "$repo_root/tests/pcm_playback_buffer/host_runtime.c" \
     "$repo_root/components/mybot_platform/src/drivers/audio/mosaico_local_aec.c" \
     "$test_dir/test_audio.c" -o "$test_build_dir/test_audio"
    python3 -c 'import subprocess,sys; subprocess.run(sys.argv[1:], check=True, timeout=30)' \
        "$test_build_dir/test_audio"
done
