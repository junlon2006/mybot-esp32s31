# Third-party notices

The root Apache-2.0 license applies to project-maintained code unless a file states otherwise. Retained components keep their own terms and file notices. Exact revisions and hashes are recorded in [VENDORED_SOURCES.md](VENDORED_SOURCES.md).

| Input | Terms | Location |
| --- | --- | --- |
| MyBot SDK v1.3.0 plus updates | Apache-2.0; bundled JSON portions MIT | `components/mybot_stack/mybot_sdk/mybot` |
| AOSL | bundled Apache-2.0 text with additional deployment restrictions | `components/mybot_stack/aosl/LICENSE` |
| Agora RTSA Lite ESP32-S31 v1.10.1 build 1366494 | separate Agora distribution terms | `components/mybot_stack/agora_rtc` |
| esp-wifi-connect 3.2.2 | MIT | `components/esp-wifi-connect/LICENSE` |
| esp_codec_dev 1.5.9 (ES8311 used) | Apache-2.0 | `components/esp_codec_dev/LICENSE` and file notices |
| LVGL 9.5.0 | MIT plus bundled font/library-specific terms | `components/lvgl/LICENCE.txt` and retained package licenses |
| Espressif LVGL port 2.8.0~1 | Apache-2.0 | `components/esp_lvgl_port/license.txt` |
| Espressif base LCD touch 1.2.1 | Apache-2.0 | generated `managed_components/espressif__esp_lcd_touch` package license |
| cJSON 1.7.19~2 | MIT | generated `managed_components/espressif__cjson` package license |
| Espressif TinyUSB driver 2.4.0 | Apache-2.0 | generated `managed_components/espressif__esp_tinyusb/LICENSE` and file notices |
| TinyUSB 0.19.0~3 | MIT plus retained file notices | generated `managed_components/espressif__tinyusb/LICENSE` and package file notices |
| Espressif USB Device UAC 1.3.1 and CMake Utilities 1.3.0 | Apache-2.0 plus retained file notices | UAC headers/configuration in production Mosaico; full runtime and CMake Utilities in the standalone diagnostic. Licenses remain under generated `managed_components/espressif__usb_device_uac/license.txt` and `espressif__cmake_utilities/license.txt` |
| Pairing prompt and digit recordings | Apache-2.0, SDK asset provenance | `components/mybot_platform/assets/locales/`, `SOURCES.json` and the retained MyBot license |
| Noto Sans SC UI glyph subset | SIL Open Font License 1.1 | `components/mybot_platform/src/drivers/display/assets/licenses/LVGL_VIEW_FONT_LICENSE.txt` |
| ESP-IDF v6.1 | Apache-2.0 plus component-specific terms | external development SDK |

## Retained component terms

AOSL's bundled license adds material deployment restrictions to its Apache-2.0 text. The combined repository and firmware must not be described as uniformly Apache-2.0. Its full snapshot and notices remain unchanged.

The bundled S31 RTSA header/configuration/static library are retained from the user-supplied package. The package did not include a standalone license or NOTICE file; this project does not relicense that binary. Applicable Agora SDK terms continue to govern its use and distribution. The original tgz remains a local ignored input.

The esp-wifi-connect 3.2.2 manifest declares MIT, but its archive omitted the license file. The retained `LICENSE` is byte-identical to the MIT text later published upstream in commit `347682fa013b52f863052ad4b1a793ca3cabff17`.

The complete official `esp_codec_dev` 1.5.9 release, LVGL and the inherited LVGL-port display/runtime subset retain their license files and file-level notices. Existing LVGL-port integration corrections remain documented in its `PATCHES.md`; this update preserves that baseline. Local CMake wrappers select the S31 runtime sources; they do not remove licenses from the source snapshots. Managed base-touch and cJSON licenses accompany their generated packages.

## Audio and display assets

Pairing prompts and digit recordings derive from MyBot's 16 kHz mono signed-16 PCM assets at `7854c1d5798004485aa1947ff55116bdb7ab0237`, under Apache-2.0. Source/output hashes, encoding commands and provisioning-prompt details are retained in `components/mybot_platform/assets/locales/SOURCES.json`. Build-generated PCM keeps the source asset's terms.

The translation view's UI fonts and live-caption subsets use Noto Sans SC Regular (Adobe copyright 2014, 2015) and retain SIL OFL 1.1 in `LVGL_VIEW_FONT_LICENSE.txt`. The caption fonts add the 6,763 GB2312 Han characters, Latin-1 and common punctuation using the same source font. The pinned source-font hash and embedded license provenance are recorded in `VENDORED_SOURCES.md`; the original TTF is not redistributed. Built-in LVGL Montserrat and Font Awesome terms remain at `components/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt` and `FontAwesome5/LICENSE.txt`.

The upstream MyBot baseline's own notices may describe packages used by other targets. Those notices remain part of the snapshot and do not define this S31 firmware's active dependency set. Every retained package license and file-level copyright/SPDX notice remains in force.

MyBot SDK modifications for V2VT media, face-to-face and bidirectional USB meetings are maintained by this project under its existing Apache-2.0 license. Upstream copyright and license notices remain intact. The supplied Agora RTSA and official codec sources are unchanged.

## Mosaico hardware integration

The Mosaico V1.2 pin configuration, eFuse revision mapping, VCC soft-start ramp and CO5300 startup sequence derive from the official
`esp-mosaico/esp-mosaico-bsp` at `8ce87a7ef130f946de4f84f6a5a20db1d8982605` under Apache-2.0.
Espressif 2026 copyright is retained in the derived hardware files. Managed
`esp_lcd_co5300` 2.2.0 and `esp_lcd_touch_cst9220` 0.1.0 retain their package Apache-2.0
licenses under `managed_components/`. The earlier project adapter's MIT notices remain on
shared audio lifecycle and derived display/input glue. No full BSP or factory artwork is imported.

Production USB clock and speaker feature-unit control handling follows the pinned UAC 1.3.1
protocol under Apache-2.0, with Espressif 2023–2026 attribution retained in the project adapter.
The production build reuses the package's unmodified configuration headers and Kconfig
without compiling its singleton or worker runtime. Its fixed duplex descriptor graph in
`components/mybot_platform/src/drivers/usb/mosaico_uac_descriptors.h` is adapted from
UAC 1.3.1 `tusb_uac/uac_descriptors.h`, with the unused microphone Feature Unit removed.
That derived header retains the complete MIT license and copyright (c) 2020 Jerzy
Kasenbreg. TinyUSB remains the audio class and USB controller implementation.
