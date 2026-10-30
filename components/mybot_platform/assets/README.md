# Platform assets

## Announcement audio

`locales/zh-CN` and `locales/en-US` contain pairing/provisioning prompts and digits. `locales/SOURCES.json` retains their original source/output hashes, frame counts, tool versions and encoding commands. The pairing prompt and digits derive from MyBot PCM assets at commit `7854c1d5798004485aa1947ff55116bdb7ab0237` under the retained SDK Apache-2.0 license; provisioning details are recorded separately in that manifest.

The tracked Ogg files are build inputs. CMake requires **ffmpeg** and converts the selected locale to 16 kHz mono S16LE in the build directory before embedding it. The firmware uses the generated PCM and performs no runtime Ogg/Opus decoding. Generated PCM is not tracked; it retains the source asset's terms. Keep both locale source directories for reproducible Chinese/English builds.

The source Ogg files use historical 20 ms Opus packets. That encoding is independent of RTSA's required 60 ms application frames and does not change the SDK's 16 kHz mono signed-16 PCM contract. The original encoding command and hashes remain authoritative when regenerating those source assets.

## Shared LVGL images

`ui/noto_emoji` contains neutral, happy, relaxed and thinking glyphs with their original PNGs. `scripts/generate-lvgl-ui-assets.py` pads them to 64×64 and generates constant ARGB8888 pixels in `src/drivers/display/renderers/lvgl/lvgl_assets.c`; no runtime PNG decoder is required.

The rasterized Noto Color Emoji glyphs retain Google's copyright and [SIL OFL 1.1](ui/noto_emoji/LICENSE.txt). Their [SOURCES.json](ui/noto_emoji/SOURCES.json) records font/package provenance, per-file hashes and conversion details. `python3 scripts/generate-lvgl-ui-assets.py --check` checks inputs against the generated arrays.

The shared Chinese UI font retains its Source Han Sans license in `src/drivers/display/assets/licenses/LVGL_VIEW_FONT_LICENSE.txt`. Full dependency/asset notices are in the root `THIRD_PARTY_NOTICES.md`.
