#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/pcm_playback_buffer"
test_build_dir=$(mktemp -d /tmp/mybot-pcm-playback.XXXXXX)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=undefined \
    -D_POSIX_C_SOURCE=200809L -DCONFIG_MYBOT_DEBUG_RESOURCE_MONITOR=0 \
    -I"$test_dir/mocks" -I"$test_dir" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    "$test_dir/host_runtime.c" "$test_dir/test_pcm.c" -o "$test_build_dir/test_pcm"
python3 -c 'import subprocess,sys; subprocess.run(sys.argv[1:], check=True, timeout=30)' "$test_build_dir/test_pcm"
