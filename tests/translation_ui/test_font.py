#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check static UI coverage and the bounded, packed live-caption font assets."""
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


def array_body(text, name):
    match = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    assert match, "missing array " + name
    return match.group(1)


caption_text = (font.parent / "caption_fonts.c").read_text()
caption_sections = re.split(r"/\* MyBot Caption Sans: (\d+) px", caption_text)
assert set(caption_sections[1::2]) == {"20", "28"}
caption_available = {}
estimated_size = 0
for pixels, body in zip(caption_sections[1::2], caption_sections[2::2]):
    bitmap = bytes(int(value, 16) for value in re.findall(
        r"0x([0-9a-f]{2})", array_body(body, "caption_bitmap_" + pixels)))
    assert len(bitmap) < (1 << 20), "font bitmap exceeds compact descriptor indices"
    glyphs = re.findall(r"/\* U\+([0-9A-F]{4,6}) \*/\s*\{([^}]+)\}", body)
    available = {int(codepoint, 16) for codepoint, _ in glyphs}
    caption_available[pixels] = available
    unicode_list = [int(value, 16) + 0x20 for value in re.findall(
        r"0x([0-9a-f]{4})", array_body(body, "caption_unicode_list_" + pixels))]
    assert unicode_list == sorted(available), "character map and descriptors disagree"
    assert unicode_list == [int(codepoint, 16) for codepoint, _ in glyphs], (
        "sparse character-map glyph IDs do not follow descriptor order")
    assert len(glyphs) == len(available), "duplicate glyph descriptors"
    assert re.search(r"\.range_start\s*=\s*0x20\b", body)
    assert int(re.search(r"\.range_length\s*=\s*(\d+)", body).group(1)) == (
        unicode_list[-1] - 0x20 + 1)
    assert int(re.search(r"\.list_length\s*=\s*(\d+)", body).group(1)) == len(glyphs)
    assert re.search(r"\.glyph_id_start\s*=\s*1\b", body)
    assert "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY" in body
    offset = 0
    for codepoint, descriptor in glyphs:
        metrics = {key: int(value) for key, value in re.findall(
            r"\.(\w+)\s*=\s*(-?\d+)", descriptor)}
        assert metrics["bitmap_index"] == offset, "incorrect glyph bitmap index"
        assert 0 <= metrics["adv_w"] < 4096
        assert 0 <= metrics["box_w"] < 256 and 0 <= metrics["box_h"] < 256
        assert -128 <= metrics["ofs_x"] < 128 and -128 <= metrics["ofs_y"] < 128
        count = metrics["box_w"] * metrics["box_h"]
        size = (count + 3) // 4
        if count % 4:
            assert bitmap[offset + size - 1] & ((1 << (8 - 2 * (count % 4))) - 1) == 0, (
                "glyph U+%s has nonzero trailing pixel padding" % codepoint)
        offset += size
    assert offset == len(bitmap), "2 bpp bitmap size does not match glyph dimensions"
    assert re.search(r"\.bpp\s*=\s*2\b", body)
    assert "mybot_caption_font_" + pixels in body
    # Both antialiasing levels must survive the 8-bit to 2-bit quantization.
    levels = {(value >> shift) & 3 for value in bitmap for shift in (0, 2, 4, 6)}
    assert levels == {0, 1, 2, 3}
    estimated_size += len(bitmap) + (len(glyphs) + 1) * 8 + len(glyphs) * 2

han = set()
for lead in range(0xB0, 0xF8):
    for trail in range(0xA1, 0xFF):
        try:
            han.add(ord(bytes((lead, trail)).decode("gb2312")))
        except UnicodeDecodeError:
            pass
assert len(han) == 6763 and han <= caption_available["20"]
latin = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
assert latin <= caption_available["20"] and latin <= caption_available["28"]
assert {ord(c) for c in "‐‑–—‘’“”•…−□�"} <= caption_available["28"]
assert {ord(c) for c in "、。，！？：；（）【】《》〈〉「」『』〔〕［］｛｝％＋－＝＠＃＆＊～／＼｜＿　￥"} <= caption_available["20"]
assert re.search(r"\.fallback\s*=\s*&mybot_caption_font_20\b", caption_text)
assert {ord(c) for c in "CAFÉ naïve – “字幕”"} <= (
    caption_available["28"] | caption_available["20"])
assert estimated_size < 900 * 1024, "caption fonts exceed the 900 KiB flash budget"
assert "Noto Sans SC" in caption_text
print("PASS caption fonts: all 6763 GB2312 Han glyphs, Latin-1, punctuation, fallback; "
      "%d bytes including packed bitmaps and maps" % estimated_size)
