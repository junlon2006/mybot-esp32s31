# Vendored dependency baselines

This ESP32-S31 project derives from MyBot ESP32-S3 source commit `2782cc88f52c8a46419654b72a918a8273cc23bf`. The default board is `esp-mosaico-v1.2`, with the earlier `esp32-s31-korvo-1` profile retained; legacy S3 profiles, platform drivers, display components and camera/software-JPEG dependencies have been removed. Complete retained snapshots are intentionally broader than the active firmware source set.

License terms are recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Builds require ESP-IDF **v6.1**, the `esp32s31` preview target and ffmpeg. Initial toolchain: GCC 15.2.0 / `esp-15.2.0_20251204`, Newlib.

## Retained sources

| Path | Source | Pinned baseline |
| --- | --- | --- |
| `components/mybot_stack/mybot_sdk/mybot` | [junlon2006/mybot](https://github.com/junlon2006/mybot) | project-maintained V2VT media/face-to-face/meeting translation fork of v1.3.0 / `baf41bac3b6ca25bad9820f9fc857c94698607d8` |
| `components/mybot_stack/aosl` | [AgoraIO-Community/aosl](https://github.com/AgoraIO-Community/aosl) | v1.0.4 plus fixes, `84e086084ebcd0ae2455a0ce5721950c5fe2e656` |
| `components/esp-wifi-connect` | [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect) | 3.2.2, `c24b97c194e6b4a1d7be0237b3c28980661cac1e` |
| `components/esp_codec_dev` | ESP Component Registry / [espressif/esp-adf](https://github.com/espressif/esp-adf) | complete 1.5.9 source release, `df1182ab7ee63eb4e43304b526b59ed2d49b89f3`; registry file checksums verified |
| `components/lvgl` | [lvgl/lvgl](https://github.com/lvgl/lvgl) | 9.5.0, `85aa60d18b3d5e5588d7b247abf90198f07c8a63` |
| `components/esp_lvgl_port` | [espressif/esp-bsp](https://github.com/espressif/esp-bsp), `components/esp_lvgl_port` | 2.8.0~1 runtime/display subset, `d14ff131266bf1392efff88db72cb4638897507c`; inherited integration corrections in `PATCHES.md`, local CMake source selection |
| `components/mybot_platform/boards/esp32-s31-korvo-1/reference/esp32_s31_adc_calibration.c` | [official S31 BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo) | `bd4d4ac18657e73583af3d7c0b0a23c3cddf3735`; unchanged source, integration header maintained locally |

MyBot maintains V2VT media, face-to-face and bidirectional USB meeting translation: device-selected languages, validated per-direction routes, publisher-only subscription, durable session cleanup and a dedicated UI contract. Its cloud-AEC MIC/reference packing, G.722 and 60 ms cadence are retained. See `components/mybot_stack/mybot_sdk/TRANSLATION.md`. Original AOSL, codec and LVGL source snapshots remain unchanged. The inherited LVGL-port runtime/display subset and its existing lifecycle corrections are retained from the imported baseline; its `PATCHES.md` records those corrections. `components/mybot_stack/aosl` builds the portable ESP-IDF HAL from its retained `esp32-s3` source directory; the project wrapper and `s31_port/` supply S31 configuration, OS naming, audited PSRAM stacks and static task reclamation.

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
| `espressif/esp_tinyusb` | 2.4.0 | Espressif driver/PHY/descriptors for Mosaico UAC/CDC and the optional Korvo CDC probe |
| `espressif/tinyusb` | 0.19.0~3 | S31 USB Device stack, CDC and Mosaico UAC2 classes |
| `espressif/usb` | 1.6.0 | S31 USB Host stack for the optional descriptor-only probe |

GT1151/base touch, esp_tinyusb and the USB Host library retain Espressif Apache-2.0 licenses and package file notices; cJSON and TinyUSB retain MIT with their file notices. The USB versions are pinned in the application manifest; generated packages remain unmodified. Native ES8389 support comes from the official codec snapshot, not a substitute codec.

The Host probe uses public USB Host APIs described by ESP-IDF v6.1's
`examples/peripherals/usb/host/usb_host_lib`. The factory HID Host code at esp-dev-kits
`5c06bf1835329d6525f316475f7218aab6acfe79` was read as a board-initialization reference;
its application, camera and HID driver were not imported.

The separate `diagnostics/mosaico_uac` test has its own generated dependency lock:
TinyUSB 0.19.0~3, USB Device UAC 1.3.1 and CMake Utilities 1.3.0. UAC is from
esp-iot-solution `6a11ddd575777cf5b7722fcd62fd39e18a3372fa`, component hash
`e4665bee61baaee5e06acae662438ffd5ad794ce6d44b10c30173e9940b80896`.
V1.2 audio initialization follows esp-mosaico-bsp
`8ce87a7ef130f946de4f84f6a5a20db1d8982605` and the official CoreBoard V1.2 schematic.
Its Espressif attribution is retained. This test uses the existing unmodified
codec 1.5.9 source; it does not import the full IDF 6.2 BSP. The scoped FreeRTOS
and feedback compile adapter is project-maintained; managed inputs remain unchanged.
The diagnostic also compiles a generated DWC2 driver copy with the target-documented
ISO IN multi-count correction in initial scheduling/retries and the required
minimum dedicated IN TxFIFO depth of 16 words. The adapter checks
the original `dcd_dwc2.c` hash
`b02378ee5b90f4eb00fee6289ab7531432e7e41b91e618c0319890b03b9fb25f`;
the original file and its MIT notice remain intact, including in the generated copy.

## Project assets and hardware references

| Paths | Source | Pinned revision/provenance |
| --- | --- | --- |
| Pairing `prompt.ogg` and `0.ogg`–`9.ogg` under `assets/locales/{en-US,zh-CN}` | [junlon2006/mybot](https://github.com/junlon2006/mybot), `assets/locales` | `7854c1d5798004485aa1947ff55116bdb7ab0237`; PCM-to-Ogg hashes/command in `assets/locales/SOURCES.json` |
| Locale `wificonfig.ogg` | project-retained provisioning prompts | format, decoded frame counts and hashes in `assets/locales/SOURCES.json` |
| `src/drivers/display/renderers/lvgl/lvgl_fonts.c` | Noto Sans SC Regular, supplied local `NotoSansSC.ttf` | font name-table version `0.00 July 30, 2017`; SHA-256 `a0af66489baf1f306732ff16da58d86c342e0b3b4b3674079ee2a9b0aee23258`; 20/28/40 px, 4 bpp subsets generated by `scripts/generate-lvgl-font.py`; Adobe 2014/2015 copyright and SIL OFL 1.1 retained |
| `src/drivers/display/renderers/lvgl/caption_fonts.c` | same pinned Noto Sans SC Regular input above | 20 px, 2 bpp: all 6,763 GB2312 Han characters, Latin-1 and common Chinese/English punctuation; 28 px, 2 bpp: printable ASCII, Latin-1 and typographic punctuation, falling back to the 20 px subset; generated by `scripts/generate-caption-fonts.py`, Pillow 12.3.0 / FreeType 2.14.3 / clang-format 18.1.8; same Adobe copyright and SIL OFL 1.1 |

The Noto Sans SC input is a user-supplied local font; only the generated subset is retained. Its embedded name-table records identify Noto Sans SC Regular, Adobe copyright, the SIL OFL 1.1 grant and `http://www.google.com/get/noto/`. Reproduction requires the exact input hash above; no unrelated application code or assets are imported.

The native scene layout follows the user-supplied `device-translation-flow.zh-CN.html` simulator and adapts its 320×240 composition to native Mosaico 480×480 and Korvo 800×480 layouts. Local LVGL objects implement its shapes and controls; no HTML container scripts, online icon/font packages or external runtime assets are imported.

Asset paths in the table are relative to `components/mybot_platform/`. ffmpeg converts existing Ogg inputs to S16LE inside `build/`; no S3 audio decoder is linked. Original attribution applies to the derived PCM.

Hardware facts come from the [official ESP32-S31-Korvo-1 guide](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html) and the fixed BSP above. The project follows the guide's ES8389 TDM requirement rather than the factory demo's standard-I2S bus configuration. RGB and GT1151 adapters are project-maintained. Factory coffee UI/assets, camera, SD and USB application paths were not imported. No external reference repository was modified.

The official codec source files retain their original bytes and line endings. The optional Xtensa-only `device/zl38063/firmware/libfirmware.a` is not versioned or used by S31; the board rejects `CONFIG_CODEC_ZL38063_SUPPORT`. This optional binary is unrelated to the approved S31 RTSA input.

The `esp-wifi-connect` CMake wrapper explicitly declares its IDF 6 cJSON dependency, including builds that use the already-resolved managed sources with the component manager disabled. Its application sources remain unchanged.

## Mosaico V1.2 translation board

The production `esp-mosaico-v1.2` profile uses hardware facts from the official guide and
CoreBoard V1.2 schematic linked in [the board guide](docs/esp-mosaico-v1.2.zh-CN.md).
Power/audio/display configuration is based on `esp-mosaico/esp-mosaico-bsp` commit
`8ce87a7ef130f946de4f84f6a5a20db1d8982605`. Its `onboard/display.c` Git blob
`aa8b6566c39306868e25619d9b865c1cc156cb3d` supplies the CO5300 initialization sequence;
Espressif copyright and Apache-2.0 attribution remain in the project adapter. The full BSP,
bootloader splash, NAND, camera, sensors and sample UI are not imported.

The eFuse USER_DATA revision mapping, retained output-level detection and active-low VCC
soft start derive from the same pinned BSP's `onboard/esp_mosaico.c`, Git blob
`f6c5efa192638e9cce674ed395fb1eb1de691fa4`. The adapter retains the official low-speed
LEDC timer1/channel1, 100 kHz / 8-bit duty sweep from 255 to 0, the Kconfig default
50 ms duration, and the audio startup's following 10 ms settle. Project fault handling
restores GPIO ownership and attempts to leave a failed ramp off without uninstalling
another owner's fade service. Known V1.0 hardware is rejected before pin access;
V1.1 and V1.2 share the official V1.2 map. Unlike upstream's rejection of all unknown
revisions, this fixed-target firmware explicitly warns and assumes V1.2 for blank or
unknown eFuse data. This policy does not establish electrical compatibility.

Managed display inputs are `espressif/esp_lcd_co5300` **2.2.0** and
`espressif/esp_lcd_touch_cst9220` **0.1.0**, with exact content hashes in `dependencies.lock`.
They use the existing LCD touch base and shared board-owned I2C0. The production board retains
IDF 6.1, LVGL 9.5.0 and codec 1.5.9. The shared audio lifecycle was extracted from the project
Korvo adapter with its original MIT header; board-specific configuration remains separate.

Mosaico touch orientation is overridden in the project adapter with `mirror_y=true`, based
on recorded physical Home/volume taps on the user's V1.2 assembly. The common touch getter
performs `479-y`; this does not alter the pinned BSP references or managed CST9220 sources.

## Mosaico composite USB transport

Production Mosaico uses the existing `espressif/esp_tinyusb` 2.4.0 and TinyUSB 0.19.0~3
with `usb_device_uac` 1.3.1 in AS_PART mode. The UAC manifest belongs to the nested
`boards/esp-mosaico-v1.2/mybot_mosaico_usb` component, registered only by that profile;
Korvo does not discover or compile UAC. Project-owned descriptors/configuration combine
one duplex UAC2 function and one CDC function. The production wrapper reuses the diagnostic's
hash-checked generated DWC2 compilation copy and scoped ISR shim; the UAC singleton is placed
in internal RAM because production enables PSRAM. Vendor sources remain unchanged.
The project-owned USB OUT adapter drains into an owned bounded ring, then filters and
decimates 48 kHz stereo to 16 kHz mono in the capture worker. Meeting SPEAK PCM is
interpolated from 16 kHz mono to a bounded 48 kHz queue and sent on host-driven USB IN
transfers. The filters and occupancy rate controls are project implementations. The
scoped FIFO-transfer wrapper forwards non-microphone endpoints unchanged. No additional
third-party DSP implementation is imported.


The S31 AOSL component wrapper force-includes a project task-stack adapter only for
`platform/src/esp32-s3/aosl_hal_thread.c`. Audited application workers use PSRAM stacks;
control and vendor RTSA tasks retain internal stacks. Matching deletion is selected by
actual stack placement. The original AOSL source bytes and RTSA binary remain unchanged.
PSRAM AOSL self-exit transfers the task to a permanent static internal reaper, prepared
before any PSRAM worker. It uses IDF's external WithCaps deletion protocol and allocates
no temporary task at exit. The queue, TCB and aligned 2 KiB reaper stack stay internal.
