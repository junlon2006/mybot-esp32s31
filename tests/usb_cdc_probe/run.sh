#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/mybot-usb-cdc-probe.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
case "$(uname -s)" in
    Darwin) strip_flag=-Wl,-dead_strip ;;
    *) strip_flag=-Wl,--gc-sections ;;
esac
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wno-unused-function \
    -DCONFIG_TINYUSB_SUSPEND_CALLBACK=1 -DCONFIG_TINYUSB_RESUME_CALLBACK=1 \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections "$strip_flag" \
    -I"$test_dir/mocks" "$test_dir/test_probe.c" -o "$test_binary"
"$test_binary"
