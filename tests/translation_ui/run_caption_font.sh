#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
caption_source="$repo_root/components/mybot_platform/src/drivers/display/renderers/lvgl/caption_fonts.c"
test_build_dir=$(mktemp -d)
trap 'rm -r -- "$test_build_dir"' EXIT HUP INT TERM
case "$(uname -s)" in
    Darwin) strip_flag=-Wl,-dead_strip ;;
    *) strip_flag=-Wl,--gc-sections ;;
esac
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
    ${TEST_SANITIZE_FLAGS:--fsanitize=undefined -fno-sanitize-recover=all} \
    -DLV_KCONFIG_IGNORE -DLV_CONF_INCLUDE_SIMPLE \
    -ffunction-sections -fdata-sections "$strip_flag" \
    -I"$repo_root/components/lvgl" \
    "$repo_root/tests/translation_ui/test_caption_font.c" \
    "$repo_root/components/lvgl/src/font/fmt_txt/lv_font_fmt_txt.c" \
    "$repo_root/components/lvgl/src/misc/lv_utils.c" \
    "$caption_source" -o "$test_build_dir/test_caption_font"
"$test_build_dir/test_caption_font" "$test_build_dir/20.bin" "$test_build_dir/28.bin"
python3 - "$caption_source" "$test_build_dir" <<'PY'
import hashlib
from pathlib import Path
import re
import sys

sections = re.split(r"/\* MyBot Caption Sans: (\d+) px", Path(sys.argv[1]).read_text())
assert set(sections[1::2]) == {"20", "28"}
for pixels, body in zip(sections[1::2], sections[2::2]):
    expected = re.search(r"Pixels SHA256: ([0-9a-f]{64})", body)
    assert expected, "missing generation-time pixel hash: " + pixels
    decoded = Path(sys.argv[2], pixels + ".bin").read_bytes()
    assert hashlib.sha256(decoded).hexdigest() == expected.group(1), (
        pixels + " px native LVGL pixels differ from the original rendered pixels")
print("PASS native caption pixels match generation-time SHA256 at 20/28 px")
PY
