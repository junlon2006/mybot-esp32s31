# MyBot for ESP32-S31-Korvo-1 V1.1

[English](README.md) · [简体中文](README.zh-CN.md)

ESP-IDF **v6.1** firmware for `esp32-s31-korvo-1`: Wi-Fi provisioning, HTTPS pairing, RTC/RTM, ES8389 TDM voice, local prompts, ADC buttons and an 800×480 RGB/GT1151 LVGL interface. Video is unavailable in this port.

The user has confirmed basic bidirectional audio and normal screen output on the physical board. Tap the screen to start a conversation and tap again to stop; PLAY remains available. The new touch interaction still needs real-device acceptance. See the [validation record](docs/s31-validation.md) for the scope of each result.

## Build and flash

Install ESP-IDF v6.1 and ffmpeg. The pinned S31 RTSA headers/library are bundled with the project; no download or import is needed:

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
idf.py --preview -B build/korvo-s31 -p PORT flash monitor
```

Use the onboard USB-UART connector. For English, use a separate build directory/sdkconfig and append `;ci/en-us.defaults` to defaults. Development uses `idf.py` directly.

The RTSA package requires **60 ms** audio frames. ffmpeg converts the tracked Ogg prompts to 16 kHz mono S16LE in the build directory. MyBot's public audio interface remains mono signed 16-bit PCM at 16 kHz; the ES8389 bus uses four 16-bit TDM slots.

## Documentation and delivery

- [Board, controls and hardware testing](docs/esp32-s31-korvo-1.en.md)
- [Build validation and merged-image delivery](docs/RELEASING.md)
- [Platform boundaries](docs/BOARD_PORTING.md) and [UI](docs/PLATFORM_UI.md)
- [Contributing](CONTRIBUTING.md), [support](SUPPORT.md), [dependency sources](VENDORED_SOURCES.md) and [licenses](THIRD_PARTY_NOTICES.md)

The automatic builder produces both languages and checked delivery files:

```sh
python3 scripts/build_all.py --board esp32-s31-korvo-1 --language both
```

A normal builder run cleans `build/` and `releases/`; use `--no-clean` for an incremental build. Safe cleanup requires Python 3.11+. With IDF Python 3.10, use direct `idf.py` or an incremental builder run.

Only the S31 board is supported. The runtime components `mybot_sdk`, `aosl` and `agora_rtc` live together under `components/mybot_stack`, matching the ESP32 project layout. Immutable MyBot/AOSL/codec/LVGL snapshots are retained; S31 component wrappers and platform drivers are maintained separately. The approved pinned RTSA payload is tracked; the original tgz, local configuration, build outputs and firmware remain ignored. Push/PR CI automatically checks source and builds both languages using the bundled input. No release is published automatically.
