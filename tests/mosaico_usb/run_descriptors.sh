#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir="$repo_root/tests/mosaico_usb"
usb_dir="$repo_root/components/mybot_platform/src/drivers/usb"
tusb_dir=${MYBOT_TEST_TINYUSB_DIR:-"$repo_root/managed_components/espressif__tinyusb"}
esp_tusb_dir=${MYBOT_TEST_ESP_TINYUSB_DIR:-"$repo_root/managed_components/espressif__esp_tinyusb"}
uac_dir=${MYBOT_TEST_UAC_DIR:-"$repo_root/managed_components/espressif__usb_device_uac"}
for input in "$tusb_dir/src/device/usbd.h" "$esp_tusb_dir/include/tusb_config.h" "$uac_dir/tusb_uac/uac_descriptors.h"; do
    if [ ! -f "$input" ]; then
        echo "Missing pinned dependency: $input (resolve the project dependencies before this test)" >&2
        exit 1
    fi
done
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    -DCFG_TUSB_MCU=OPT_MCU_ESP32S31 '-DCFG_TUSB_CONFIG_FILE="mosaico_tusb_config.h"' \
    -I"$test_dir/descriptor_mocks" -I"$usb_dir" -I"$tusb_dir/src" \
    -I"$repo_root/components/mybot_platform/src/internal" \
    -I"$esp_tusb_dir/include" -I"$uac_dir/tusb_uac" \
    "$test_dir/test_descriptors.c" "$usb_dir/mosaico_usb_descriptors.c" \
    -o "$test_build_dir/test_descriptors"
"$test_build_dir/test_descriptors"
