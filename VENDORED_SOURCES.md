# Vendored dependency baselines

This ESP32-S31 project derives from MyBot ESP32-S3 source commit `2782cc88f52c8a46419654b72a918a8273cc23bf`. The only supported board is `esp32-s31-korvo-1`; legacy S3 profiles, platform drivers, display components and camera/software-JPEG dependencies have been removed. Complete retained snapshots are intentionally broader than the active firmware source set.

License terms are recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Builds require ESP-IDF **v6.1**, the `esp32s31` preview target and ffmpeg. Initial toolchain: GCC 15.2.0 / `esp-15.2.0_20251204`, Newlib.

## Retained sources

| Path | Source | Pinned baseline |
| --- | --- | --- |
| `components/mybot_stack/mybot_sdk/mybot` | [junlon2006/mybot](https://github.com/junlon2006/mybot) | v1.3.0 plus updates, `baf41bac3b6ca25bad9820f9fc857c94698607d8` |
| `components/mybot_stack/aosl` | [AgoraIO-Community/aosl](https://github.com/AgoraIO-Community/aosl) | v1.0.4 plus fixes, `84e086084ebcd0ae2455a0ce5721950c5fe2e656` |
| `components/esp-wifi-connect` | [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect) | 3.2.2, `c24b97c194e6b4a1d7be0237b3c28980661cac1e` |
| `components/esp_codec_dev` | ESP Component Registry / [espressif/esp-adf](https://github.com/espressif/esp-adf) | complete 1.5.9 source release, `df1182ab7ee63eb4e43304b526b59ed2d49b89f3`; registry file checksums verified |
| `components/lvgl` | [lvgl/lvgl](https://github.com/lvgl/lvgl) | 9.5.0, `85aa60d18b3d5e5588d7b247abf90198f07c8a63` |
| `components/esp_lvgl_port` | [espressif/esp-bsp](https://github.com/espressif/esp-bsp), `components/esp_lvgl_port` | 2.8.0~1 runtime/display subset, `d14ff131266bf1392efff88db72cb4638897507c`; inherited integration corrections in `PATCHES.md`, local CMake source selection |
| `components/mybot_platform/boards/esp32-s31-korvo-1/reference/esp32_s31_adc_calibration.c` | [official S31 BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo) | `bd4d4ac18657e73583af3d7c0b0a23c3cddf3735`; unchanged source, integration header maintained locally |

MyBot, AOSL, codec and LVGL source snapshots remain unchanged. The inherited LVGL-port runtime/display subset and its existing lifecycle corrections are retained unchanged from the imported baseline; its `PATCHES.md` records those corrections. `components/mybot_stack/aosl` builds the portable ESP-IDF HAL from its retained `esp32-s3` source directory; the component CMake wrapper and `s31_port/` supply S31 configuration and OS-name overrides.

## Bundled RTSA

`components/mybot_stack/agora_rtc` contains the approved S31 public header, build configuration, package metadata and RISC-V static library. They are tracked project dependencies, so a checkout builds directly without a download/import step. The original tgz and generated firmware remain ignored. `scripts/import_rtsa_s31.py` is an optional maintenance tool for restoring the exact payload from the original package; it does not run package programs.

- Package: `Agora-RTSALite-SRmRcAcAj-riscv32-rtos-elf-16-esp32s31-v1.10.1-20261030_150429-1366494.tgz`
- SDK: v1.10.1, build **1366494**.
- Original archive SHA-256: `7c6a81ab91f3b44c0cd581f84f779d7a022dac159020c19bdddd07c95da31dea`.
- `lib/riscv32/libagora-rtc-sdk.a`: `573a00d58ad19cc0b66fc1e41f17e23ada34b77fe5c7a8c3e8fa8729f73fb6c5`.
- `include/agora_rtc_api.h`: `29b1c21cbe416ee703eaa197ce623469619b1ed68cd3e9c66cb3abe2fafec7db`.
- `include/global_config.cmake`: `acf697baa23c6b11143bf35b06564c0b924ef78e9d803f91d664e7673e014c21`.

The public API header matches the prior RTSA v1.10.1 header. The package enables G.722, audio jitter buffering, RTM/channel/string-UID support and a **60 ms** minimum timer interval. Video remains disabled in this project.

The archive contains 126 RISC-V ELF32 objects and 49 x86-64 AOSL/HAL objects. The RISC-V objects use the single-float ABI, 16-byte stack alignment and GCC `esp-16.1.0_20260609`. The local RISC-V AOSL component uses whole-archive to resolve its symbols before RTSA; RTSA itself remains a normal archive. Delivery validation checks final object selection and AOSL symbol provenance. Successful builds and the user's voice interoperability test are recorded separately in [validation](docs/s31-validation.md).

## Managed dependencies

`dependencies.lock` records exact resolved versions and content hashes. Managed component directories are generated and not hand-edited:

| Component | Version | Purpose |
| --- | --- | --- |
| `espressif/esp_lcd_touch_gt1151` | 1.1.1 | GT1151 touch driver |
| `espressif/esp_lcd_touch` | 1.2.1 | base touch interface |
| `espressif/cjson` | 1.7.19~2 | JSON used by Wi-Fi provisioning |

GT1151/base touch retain Espressif Apache-2.0 licenses; cJSON retains MIT. Native ES8389 support comes from the official codec snapshot, not a substitute codec.

## Project assets and hardware references

| Paths | Source | Pinned revision/provenance |
| --- | --- | --- |
| Pairing `prompt.ogg` and `0.ogg`–`9.ogg` under `assets/locales/{en-US,zh-CN}` | [junlon2006/mybot](https://github.com/junlon2006/mybot), `assets/locales` | `7854c1d5798004485aa1947ff55116bdb7ab0237`; PCM-to-Ogg hashes/command in `assets/locales/SOURCES.json` |
| Locale `wificonfig.ogg` | project-retained provisioning prompts | format, decoded frame counts and hashes in `assets/locales/SOURCES.json` |
| `src/drivers/display/renderers/lvgl/lvgl_fonts.c` | LVGL Source Han Sans SC font subset | 9.5.0 font SHA-256 `1ee89e1669362dee13851129c0a8a791a87521eb4148e5efbf5d26596738e25b`; 20 px, 4 bpp UI subset; SIL OFL retained |
| `assets/ui/noto_emoji` and generated `lvgl_assets.c` | [78/noto-fonts](https://github.com/78/noto-fonts), rasterized Noto Color Emoji | 2.0.0, `d45dbc64052d57048f20ab1770074172ce9eb53b`; glyph provenance and PNG hashes in `SOURCES.json` |

Asset paths in the table are relative to `components/mybot_platform/`. ffmpeg converts existing Ogg inputs to S16LE inside `build/`; no S3 audio decoder is linked. Original attribution applies to the derived PCM.

Hardware facts come from the [official ESP32-S31-Korvo-1 guide](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html) and the fixed BSP above. The project follows the guide's ES8389 TDM requirement rather than the factory demo's standard-I2S bus configuration. RGB and GT1151 adapters are project-maintained. Factory coffee UI/assets, camera, SD and USB application paths were not imported. No external reference repository was modified.

The official codec source files retain their original bytes and line endings. The optional Xtensa-only `device/zl38063/firmware/libfirmware.a` is not versioned or used by S31; the board rejects `CONFIG_CODEC_ZL38063_SUPPORT`. This optional binary is unrelated to the approved S31 RTSA input.
