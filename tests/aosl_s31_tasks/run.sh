#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_build_dir=$(mktemp -d /tmp/mybot-aosl-tasks.XXXXXX)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
cmake -S "$test_dir" -B "$test_build_dir" >/dev/null
cmake --build "$test_build_dir" >/dev/null
python3 -c 'import subprocess,sys; subprocess.run(sys.argv[1:], check=True, timeout=30)' \
    "$test_build_dir/test_tasks"
