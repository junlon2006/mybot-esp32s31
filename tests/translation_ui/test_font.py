#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Catch missing Chinese UI glyphs before a successful build hides the issue."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
view = root / "components/mybot_platform/src/drivers/display/renderers/lvgl/lvgl_view.cc"
font = root / "components/mybot_platform/src/drivers/display/renderers/lvgl/lvgl_fonts.c"
language_source = root / "components/mybot_stack/mybot_sdk/mybot/src/core/mybot_ui.c"
source = view.read_text() + language_source.read_text()
needed = {ord(character) for character in source if ord(character) > 127}
text = font.read_text()
sections = re.split(r"/\* MyBot UI Sans: (\d+) px", text)
assert set(sections[1::2]) == {"20", "28", "40"}
for pixels, body in zip(sections[1::2], sections[2::2]):
    available = {int(value, 16) for value in re.findall(r"U\+([0-9A-F]{4,6})", body)}
    missing = needed - available
    assert not missing, "%s px missing: %s" % (
        pixels, ", ".join("U+%04X" % value for value in sorted(missing)))
    assert "mybot_lvgl_font_%s" % pixels in body
assert "Noto Sans SC" in text
print("PASS translation UI fonts: %d glyphs available at 20/28/40 px" % len(needed))
