# Platform assets

## Announcement audio

`locales/zh-CN` and `locales/en-US` contain pairing/provisioning prompts and digits. `locales/SOURCES.json` retains their original source/output hashes, frame counts, tool versions and encoding commands. The pairing prompt and digits derive from MyBot PCM assets at commit `7854c1d5798004485aa1947ff55116bdb7ab0237` under the retained SDK Apache-2.0 license; provisioning details are recorded separately in that manifest.

The tracked Ogg files are build inputs. CMake requires **ffmpeg** and converts the selected locale to 16 kHz mono S16LE in the build directory before embedding it. The firmware uses the generated PCM and performs no runtime Ogg/Opus decoding. Generated PCM is not tracked; it retains the source asset's terms. Keep both locale source directories for reproducible Chinese/English builds.

The source Ogg files use historical 20 ms Opus packets. That encoding is independent of RTSA's required 60 ms application frames and does not change the SDK's 16 kHz mono signed-16 PCM contract. The original encoding command and hashes remain authoritative when regenerating those source assets.

## Shared LVGL fonts

The Chinese translation UI uses generated 20/28/40 px Noto Sans SC Regular subsets and retains SIL OFL 1.1 in `src/drivers/display/assets/licenses/LVGL_VIEW_FONT_LICENSE.txt`. Scene shapes, icons and drag controls use local LVGL objects. Full dependency/asset notices are in the root `THIRD_PARTY_NOTICES.md`.

Live captions use a separate 20 px, 2 bpp subset containing all 6,763 GB2312 Han characters, printable ASCII, Latin-1 and common Chinese/English punctuation. The 28 px, 2 bpp caption font contains printable ASCII, Latin-1 and typographic punctuation and falls back to the 20 px font. These subsets support the default Chinese-to-English scenario; they do not provide complete CJK, Arabic, Indic or other multilingual coverage. Unavailable glyphs use LVGL's missing-glyph placeholder. The generated bitmaps are ordinary flash constants; no runtime font file or extra partition is needed.

To reproduce `caption_fonts.c`, use the exact source font hash in `VENDORED_SOURCES.md`, Pillow 12.3.0, FreeType 2.14.3 and clang-format 18.1.8:

```sh
python3 scripts/generate-caption-fonts.py --font /path/to/NotoSansSC.ttf
python3 tests/translation_ui/test_font.py
```
