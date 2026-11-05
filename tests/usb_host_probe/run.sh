#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-usb-host-probe.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -I"$test_dir/mocks" -I"$test_dir/../../main" \
    "$test_dir/test_probe.c" "$test_dir/../../main/usb_host_probe.c" -o "$test_binary"
for scenario in normal idle task_failure install_failure register_failure \
    cleanup_pending cleanup_timeout cleanup_error free_failure uninstall_failure open_failure info_failure \
    device_descriptor_failure config_descriptor_failure close_retry queued_replug \
    close_failure library_error client_error; do
    "$test_binary" "$scenario"
done
