#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate compressed Chinese/Japanese/Korean/English 2 bpp subtitle fonts.

Usage: python3 scripts/generate-caption-fonts.py --font path/to/NotoSansSC.ttf
Requires Pillow 12.3.0 / FreeType 2.14.3 and clang-format 18.1.8 for byte-for-byte
reproduction. The generated output retains the source font's SIL OFL 1.1 license.
"""

import argparse
import hashlib
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version, features


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "components/mybot_platform/src/drivers/display/renderers/lvgl/caption_fonts.c"
FONT_SHA256 = "a0af66489baf1f306732ff16da58d86c342e0b3b4b3674079ee2a9b0aee23258"
LATIN = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
PUNCTUATION = {ord(c) for c in "‐‑–—‘’“”•…−□�"}
CJK_PUNCTUATION = {ord(c) for c in "、。，！？：；（）【】《》〈〉「」『』〔〕［］｛｝％＋－＝＠＃＆＊～／＼｜＿　￥"}
FONT_BUDGET = 2 * 1024 * 1024


def gb2312_han():
    codepoints = set()
    for lead in range(0xB0, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                codepoints.add(ord(bytes((lead, trail)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    assert len(codepoints) == 6763
    return codepoints


def japanese():
    """JIS X 0208 plus Windows Japanese extensions, excluding private-use glyphs."""
    codepoints = set()
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
                    codepoints.add(ord(character))
    return (codepoints | set(range(0x3041, 0x3097)) | set(range(0x3099, 0x30A0))
            | set(range(0x30A0, 0x3100)) | set(range(0x31F0, 0x3200))
            | set(range(0xFF61, 0xFFA0)))


def korean():
    """All modern Hangul syllables, Jamo, compatibility and assigned halfwidth letters."""
    return (set(range(0xAC00, 0xD7A4)) | set(range(0x1100, 0x1200))
            | set(range(0x3131, 0x318F)) | set(range(0xFFA0, 0xFFBF))
            | set(range(0xFFC2, 0xFFC8)) | set(range(0xFFCA, 0xFFD0))
            | set(range(0xFFD2, 0xFFD8)) | set(range(0xFFDA, 0xFFDD)))


def compress_2bpp(coverage, width):
    """Encode LVGL row-XOR RLE, keeping the original four alpha levels."""
    pixels = list(coverage)
    for index in range(len(pixels) - 1, width - 1, -1):
        pixels[index] ^= pixels[index - width]
    output = bytearray()
    accumulator = available = 0

    def emit(value, bits):
        nonlocal accumulator, available
        accumulator = (accumulator << bits) | value
        available += bits
        while available >= 8:
            available -= 8
            output.append((accumulator >> available) & 255)

    state = position = previous = count = 0
    while position < len(pixels):
        value = pixels[position]
        if state == 0:
            emit(value, 2)
            if position and value == previous:
                state, count = 1, 0
            previous = value
        elif state == 1:
            if value != previous:
                emit(0, 1)
                emit(value, 2)
                previous, state = value, 0
            else:
                emit(1, 1)
                count += 1
                if count == 11:
                    count = 1
                    while (count < 63 and position + count < len(pixels)
                           and pixels[position + count] == value):
                        count += 1
                    emit(count, 6)
                    state = 2
        else:
            count -= 1
            if count == 0:
                emit(value, 2)
                previous, state = value, 0
        position += 1
    if available:
        output.append((accumulator << (8 - available)) & 255)
    # LVGL get_bits() reads one extra byte even at an exact-byte boundary.
    return bytes(output) + b"\0"


def rows(values, width=12):
    return "\n".join(
        "    " + ", ".join(values[index : index + width]) + ","
        for index in range(0, len(values), width)
    )


def render_font(font_path, pixels, codepoints, fallback=None, font_id=None):
    font_id = font_id or str(pixels)
    font = ImageFont.truetype(str(font_path), pixels)
    missing = bytes(font.getmask("\u0378"))
    bitmap = []
    pixels_sha256 = hashlib.sha256()
    descriptors = ["    /* Glyph ID 0 is reserved. */\n    {0},"]
    ascent = descent = 0
    for codepoint in codepoints:
        character = chr(codepoint)
        if bytes(font.getmask(character)) == missing:
            raise ValueError(f"Missing glyph U+{codepoint:04X}")
        x0, y0, x1, y1 = font.getbbox(character, anchor="ls")
        width, height = x1 - x0, y1 - y0
        if height == 0:
            width = 0
        advance = round(font.getlength(character) * 16)
        image = None
        if width and height:
            image = Image.new("L", (width, height))
            ImageDraw.Draw(image).text((-x0, -y0), character, font=font, fill=255, anchor="ls")
            if codepoint in (0x3099, 0x309A):
                # Noto SC stores standalone marks in a full-width cell. Overlay
                # their ink at the preceding kana's upper-right corner instead.
                ink = image.getbbox()
                image = image.crop(ink)
                width, height = image.size
                x0, y0 = -width, y0 + ink[1]
                y1, advance = y0 + height, 0
        if not (0 <= advance < 4096 and 0 <= width < 256 and 0 <= height < 256
                and -128 <= x0 < 128 and -128 <= -y1 < 128):
            raise ValueError(f"Glyph U+{codepoint:04X} exceeds LV_FONT_FMT_TXT_LARGE=0 metrics")
        descriptors.append(
            "    /* U+%04X */\n"
            "    { %d, %d, %d, %d, %d, %d },"
            % (codepoint, len(bitmap), advance, width, height, x0, -y1)
        )
        if width and height:
            coverage = [(pixel * 3 + 127) // 255 for pixel in image.get_flattened_data()]
            pixels_sha256.update(bytes(value * 85 for value in coverage))
            bitmap.extend(compress_2bpp(coverage, width))
            ascent = max(ascent, -y0)
            descent = max(descent, y1)
    if len(bitmap) >= 1 << 20:
        raise ValueError("Font exceeds LV_FONT_FMT_TXT_LARGE=0 bitmap index")
    if codepoints[-1] - codepoints[0] >= 65535:
        raise ValueError("Font exceeds the 16-bit sparse character map range")
    fallback_text = f"    .fallback = &{fallback},\n" if fallback else ""
    # Byte strings keep the exact data while avoiding millions of initializer
    # tokens in the compiler and formatter. Explicit array length omits the NUL.
    bitmap_lines = "\n".join(
        '    "' + "".join(f"\\x{value:02x}" for value in bitmap[start:start + 4096]) + '"'
        for start in range(0, len(bitmap), 4096)
    )
    text = f"""/* MyBot Caption Sans: {pixels} px, 2 bpp subset of Noto Sans SC Regular.
 * Copyright 2014, 2015 Adobe Systems Incorporated (http://www.adobe.com/).
 * See LVGL_VIEW_FONT_LICENSE.txt. Generated by scripts/generate-caption-fonts.py.
 * Input SHA256: {FONT_SHA256}
 * Pixels SHA256: {pixels_sha256.hexdigest()}
 * Font ID: {font_id}
 * Pillow {pillow_version}; FreeType {features.version("freetype2")}; {len(codepoints)} glyphs.
 */

/* Keep generated data arrays in fixed columns without expensive reflow. */
/* clang-format off */
static const uint8_t caption_bitmap_{font_id}[{len(bitmap)}] MYBOT_CAPTION_BINARY = {{
{bitmap_lines}
}};
/* clang-format on */

/* clang-format off */
/* bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y (LVGL compact glyph descriptor). */
static const lv_font_fmt_txt_glyph_dsc_t caption_glyph_dsc_{font_id}[] = {{
{chr(10).join(descriptors)}
}};
/* clang-format on */

/* clang-format off */
static const uint16_t caption_unicode_list_{font_id}[] = {{
{rows([f"0x{codepoint - 0x20:04x}" for codepoint in codepoints])}
}};
/* clang-format on */

static const lv_font_fmt_txt_cmap_t caption_cmaps_{font_id}[] = {{
    {{
        .range_start = 0x20,
        .range_length = {codepoints[-1] - 0x20 + 1},
        .glyph_id_start = 1,
        .unicode_list = caption_unicode_list_{font_id},
        .list_length = {len(codepoints)},
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
    }},
}};

static const lv_font_fmt_txt_dsc_t caption_font_dsc_{font_id} = {{
    .glyph_bitmap = caption_bitmap_{font_id},
    .glyph_dsc = caption_glyph_dsc_{font_id},
    .cmaps = caption_cmaps_{font_id},
    .cmap_num = 1,
    .bpp = 2,
    .bitmap_format = LV_FONT_FMT_TXT_COMPRESSED,
}};

const lv_font_t mybot_caption_font_{font_id} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {ascent + descent + 2},
    .base_line = {descent + 1},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &caption_font_dsc_{font_id},
{fallback_text}}};
"""
    # Compact descriptors are 8 bytes; sparse Unicode entries are 2 bytes.
    return text, len(bitmap), len(bitmap) + (len(codepoints) + 1) * 8 + len(codepoints) * 2


def generate(font_path):
    if hashlib.sha256(font_path.read_bytes()).hexdigest() != FONT_SHA256:
        raise ValueError("Unexpected font revision; verify its provenance and license first")
    fonts = [
        # Keep each compact descriptor's bitmap index below 1 MiB. Hangul is a
        # separate real fallback, without widening every existing descriptor.
        render_font(font_path, 20, sorted(korean()), font_id="ko_20"),
        render_font(font_path, 20,
                    sorted(LATIN | PUNCTUATION | CJK_PUNCTUATION | gb2312_han() | japanese()),
                    "mybot_caption_font_ko_20"),
        render_font(font_path, 28, sorted(LATIN | PUNCTUATION), "mybot_caption_font_20"),
    ]
    estimated_size = sum(size for _, _, size in fonts)
    if estimated_size >= FONT_BUDGET:
        raise ValueError(f"Caption fonts exceed the 2 MiB budget: {estimated_size}")
    OUTPUT.write_text("/* SPDX-License-Identifier: OFL-1.1 */\n#include \"lvgl.h\"\n\n"
                      "#if __has_attribute(nonstring)\n"
                      "#define MYBOT_CAPTION_BINARY __attribute__((nonstring))\n"
                      "#else\n#define MYBOT_CAPTION_BINARY\n#endif\n\n"
                      + "\n".join(text for text, _, _ in fonts)
                      + "\n#undef MYBOT_CAPTION_BINARY\n")
    subprocess.run(["clang-format", "-i", str(OUTPUT)], check=True)
    print(f"Generated {OUTPUT.relative_to(ROOT)}: "
          f"{sum(size for _, size, _ in fonts)} bitmap bytes, "
          f"{estimated_size} bytes including glyph descriptors and maps")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path)
    args = parser.parse_args()
    generate(args.font)
