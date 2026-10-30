# Third-party notices

The root Apache-2.0 license applies to project-maintained code unless a file states otherwise. Retained components keep their own terms and file notices. Exact revisions and hashes are recorded in [VENDORED_SOURCES.md](VENDORED_SOURCES.md).

| Input | Terms | Location |
| --- | --- | --- |
| MyBot SDK v1.3.0 plus updates | Apache-2.0; bundled JSON portions MIT | `components/mybot_stack/mybot_sdk/mybot` |
| AOSL | bundled Apache-2.0 text with additional deployment restrictions | `components/mybot_stack/aosl/LICENSE` |
| Agora RTSA Lite ESP32-S31 v1.10.1 build 1366494 | separate Agora distribution terms | `components/mybot_platform/ports/agora_rtc/vendor` |
| esp-wifi-connect 3.2.2 | MIT | `components/esp-wifi-connect/LICENSE` |
| esp_codec_dev 1.5.9, including ES8389 | Apache-2.0 | `components/esp_codec_dev/LICENSE` and file notices |
| LVGL 9.5.0 | MIT plus bundled font/library-specific terms | `components/lvgl/LICENCE.txt` and retained package licenses |
| Espressif LVGL port 2.8.0~1 | Apache-2.0 | `components/esp_lvgl_port/license.txt` |
| Espressif GT1151 1.1.1 and base LCD touch 1.2.1 | Apache-2.0 | generated `managed_components/espressif__esp_lcd_touch_gt1151` and `espressif__esp_lcd_touch` package licenses |
| cJSON 1.7.19~2 | MIT | generated `managed_components/espressif__cjson` package license |
| Official S31 ADC calibration source | Apache-2.0; copyright 2026 Espressif Systems (Shanghai) CO LTD | `components/mybot_platform/boards/esp32-s31-korvo-1/reference/esp32_s31_adc_calibration.c` |
| Pairing prompt and digit recordings | Apache-2.0, SDK asset provenance | `components/mybot_platform/assets/locales/`, `SOURCES.json` and the retained MyBot license |
| Source Han Sans SC UI glyph subset | SIL Open Font License 1.1 | `components/mybot_platform/src/drivers/display/assets/licenses/LVGL_VIEW_FONT_LICENSE.txt` |
| Noto Color Emoji rasterized glyphs | SIL Open Font License 1.1 | `components/mybot_platform/assets/ui/noto_emoji/LICENSE.txt` |
| ESP-IDF v6.1 | Apache-2.0 plus component-specific terms | external development SDK |

## Retained component terms

AOSL's bundled license adds material deployment restrictions to its Apache-2.0 text. The combined repository and firmware must not be described as uniformly Apache-2.0. Its full snapshot and notices remain unchanged.

The bundled S31 RTSA header/configuration/static library are retained from the user-supplied package. The package did not include a standalone license or NOTICE file; this project does not relicense that binary. Applicable Agora SDK terms continue to govern its use and distribution. The original tgz remains a local ignored input.

The esp-wifi-connect 3.2.2 manifest declares MIT, but its archive omitted the license file. The retained `LICENSE` is byte-identical to the MIT text later published upstream in commit `347682fa013b52f863052ad4b1a793ca3cabff17`.

The complete official `esp_codec_dev` 1.5.9 release, LVGL and the inherited LVGL-port display/runtime subset retain their license files and file-level notices. Existing LVGL-port integration corrections remain documented in its `PATCHES.md`; this update preserves that baseline. Local CMake wrappers select the S31 runtime sources; they do not remove licenses from the source snapshots. Managed GT1151, base-touch and cJSON licenses accompany their generated packages.

## Hardware reference and ADC source

ADC software calibration is copied unchanged from [Espressif's S31 BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo) at commit `bd4d4ac18657e73583af3d7c0b0a23c3cddf3735`. The imported file keeps its Espressif copyright and Apache-2.0 header. Its small integration header and the platform RGB/touch/audio adapters are maintained locally. Factory demo coffee images, fonts and UI application were not imported.

## Audio and display assets

Pairing prompts and digit recordings derive from MyBot's 16 kHz mono signed-16 PCM assets at `7854c1d5798004485aa1947ff55116bdb7ab0237`, under Apache-2.0. Source/output hashes, encoding commands and provisioning-prompt details are retained in `components/mybot_platform/assets/locales/SOURCES.json`. Build-generated PCM keeps the source asset's terms.

The shared view's Chinese glyph subset uses Source Han Sans SC and retains SIL OFL 1.1 in `LVGL_VIEW_FONT_LICENSE.txt`. Built-in LVGL Montserrat and Font Awesome terms remain at `components/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt` and `FontAwesome5/LICENSE.txt`.

The four local state emoji are rasterized Noto Color Emoji glyphs supplied by the pinned `78/noto-fonts` package. Their original PNGs, Google copyright, SIL OFL 1.1 and per-file provenance remain under `components/mybot_platform/assets/ui/noto_emoji/`.

The immutable MyBot snapshot's own notices may describe packages used by other targets. Those notices remain part of the snapshot and do not define this S31 firmware's active dependency set. Every retained package license and file-level copyright/SPDX notice remains in force.
