#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check static UI coverage and bounded Chinese/Japanese/Korean/English caption fonts."""
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
    match = re.search(r"\b" + name + r"\[\d*\]\s*(?:MYBOT_CAPTION_BINARY\s*)?=\s*\{(.*?)\n\};", text, re.S)
    assert match, "missing array " + name
    return match.group(1)


caption_text = (font.parent / "caption_fonts.c").read_text()
caption_sections = re.split(r"/\* MyBot Caption Sans: (\d+) px", caption_text)
assert set(caption_sections[1::2]) == {"20", "28"}
caption_available = {}
estimated_size = 0
for pixels, body in zip(caption_sections[1::2], caption_sections[2::2]):
    font_id = re.search(r"Font ID: (\w+)", body).group(1)
    bitmap = bytes(int(value, 16) for value in re.findall(
        r"\\x([0-9a-f]{2})", array_body(body, "caption_bitmap_" + font_id)))
    assert int(re.search(r"caption_bitmap_" + font_id + r"\[(\d+)\]", body).group(1)) == len(bitmap)
    assert len(bitmap) < (1 << 20), "font bitmap exceeds compact descriptor indices"
    glyphs = re.findall(r"/\* U\+([0-9A-F]{4,6}) \*/\s*\{([^}]+)\}", body)
    available = {int(codepoint, 16) for codepoint, _ in glyphs}
    assert font_id not in caption_available
    caption_available[font_id] = available
    unicode_list = [int(value, 16) + 0x20 for value in re.findall(
        r"0x([0-9a-f]{4})", array_body(body, "caption_unicode_list_" + font_id))]
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
    for index, (codepoint, descriptor) in enumerate(glyphs):
        values = [int(value) for value in re.findall(r"-?\d+", descriptor)]
        assert len(values) == 6
        metrics = dict(zip(("bitmap_index", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y"), values))
        start = metrics["bitmap_index"]
        assert start == offset, "incorrect glyph bitmap index"
        assert 0 <= metrics["adv_w"] < 4096
        assert 0 <= metrics["box_w"] < 256 and 0 <= metrics["box_h"] < 256
        assert -128 <= metrics["ofs_x"] < 128 and -128 <= metrics["ofs_y"] < 128
        count = metrics["box_w"] * metrics["box_h"]
        next_offset = (int(re.search(r"\d+", glyphs[index + 1][1]).group(0))
                       if index + 1 < len(glyphs) else len(bitmap))
        assert start <= next_offset <= len(bitmap)
        if count:
            assert next_offset > start and bitmap[next_offset - 1] == 0, (
                "glyph U+%s lacks safe RLE lookahead" % codepoint)
        else:
            assert next_offset == start
        offset = next_offset
    assert re.search(r"\.bpp\s*=\s*2\b", body)
    assert "mybot_caption_font_" + font_id in body
    assert ".bitmap_format = LV_FONT_FMT_TXT_COMPRESSED" in body
    assert re.search(r"Pixels SHA256: [0-9a-f]{64}", body)
    estimated_size += len(bitmap) + (len(glyphs) + 1) * 8 + len(glyphs) * 2
assert set(caption_available) == {"20", "28", "ko_20"}

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
assert re.search(r"\.fallback\s*=\s*&mybot_caption_font_ko_20\b", caption_text)
assert {ord(c) for c in "CAFÉ naïve – “字幕”"} <= (
    caption_available["28"] | caption_available["20"])
japanese = set(range(0x3041, 0x3097)) | set(range(0x3099, 0x3100))
japanese |= set(range(0x31F0, 0x3200)) | set(range(0xFF61, 0xFFA0))
for encoding, leads, trails in (
    ("euc_jp", range(0xA1, 0xFF), range(0xA1, 0xFF)),
    ("cp932", list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD)),
     list(range(0x40, 0x7F)) + list(range(0x80, 0xFD))),
):
    for lead in leads:
        for trail in trails:
            try:
                character = bytes((lead, trail)).decode(encoding)
            except UnicodeDecodeError:
                continue
            if len(character) == 1 and not 0xE000 <= ord(character) <= 0xF8FF:
                japanese.add(ord(character))
assert japanese <= caption_available["20"], "Japanese font coverage is incomplete"
for sample in ("日本語の翻訳を開始します。", "ガイド・ヴォイス、ﾊﾟｰﾃｨｰ。",
               "か\u3099／は\u309a", "々〆〇ゝゞヽヾ", "髙橋・﨑山"):
    assert {ord(c) for c in sample} <= caption_available["20"], sample
for codepoint in ("3099", "309A"):
    descriptor = re.search(r"/\* U\+" + codepoint + r" \*/\s*\{([^}]+)\}", caption_text).group(1)
    values = [int(value) for value in re.findall(r"-?\d+", descriptor)]
    assert values[1] == 0 and values[4] < 0
korean = set(range(0xAC00, 0xD7A4)) | set(range(0x1100, 0x1200))
korean |= set(range(0x3131, 0x318F)) | set(range(0xFFA0, 0xFFBF))
korean |= set(range(0xFFC2, 0xFFC8)) | set(range(0xFFCA, 0xFFD0))
korean |= set(range(0xFFD2, 0xFFD8)) | set(range(0xFFDA, 0xFFDD))
assert len(set(range(0xAC00, 0xD7A4))) == 11172
assert korean <= caption_available["ko_20"], "Korean font coverage is incomplete"
for sample in ("한국어 번역을 시작합니다.", "안녕하세요! 회의 자막이 정상적으로 표시됩니다.",
               "가각힣", "한글", "ㄱㄲㄳㅏㅐㅑ", "ﾡￂ"):
    assert {ord(c) for c in sample} <= (caption_available["20"] | caption_available["ko_20"]), sample
assert estimated_size < 2 * 1024 * 1024, "caption fonts exceed the 2 MiB flash budget"
assert "Noto Sans SC" in caption_text
assert "#define LV_USE_FONT_COMPRESSED 1" in (root / "components/lvgl/lv_conf.h").read_text()
print("PASS caption fonts: GB2312, JIS X 0208/CP932, kana, full modern Hangul/Jamo, "
      "Latin-1, punctuation, fallback; "
      "%d bytes including packed bitmaps and maps" % estimated_size)
