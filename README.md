# mybot-esp32s31

[![CI](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml/badge.svg)](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/junlon2006/mybot-esp32s31)](LICENSE)

**[English](README.md) | [简体中文](README.zh-CN.md)**

`mybot-esp32s31` is an ESP-IDF firmware project for standalone V2VT translation on the
**ESP-Mosaico CoreBoard V1.2**, the target for future development. The media scene sends board microphone audio to the cloud and plays
translated speech through the board speaker, initially Chinese to English. Face-to-face supports
alternating Chinese/English speech, with language buttons selecting the microphone route. The project maintains
the MyBot translation SDK fork and its board, network, audio, display and lifecycle integration.
The development API is `https://mybot.sh3t.agoralab.co/api`.

The development baseline is **ESP-IDF v6.1**. The default board profile is
`esp-mosaico-v1.2`; the earlier `esp32-s31-korvo-1` profile remains available. Flash, Octal PSRAM, the partition table, and hardware drivers are selected
with `MYBOT_BOARD`. See the
[board guide](docs/esp-mosaico-v1.2.zh-CN.md) for hardware details, controls, build commands,
and current limitations.

> Project-maintained code is Apache-2.0 unless a file says otherwise. Bundled dependencies and
> media assets have separate terms. Read [License and dependencies](#license-and-dependencies)
> before redistributing source or firmware.

## Features

- Wi-Fi station reconnect and first-boot web provisioning.
- NVS persistence for device credentials, language selection, session recovery and speaker volume.
- HTTPS with `esp-tls`, the system CA bundle, SNI, and hostname verification.
- 16 kHz mono signed-16 PCM capture and playback with 60 ms packets for the bundled RTSA
  package; other packet durations require a matching RTSA build.
- Agora RTSA microphone/reference uplink with Cloud AEC and optional AI QoS (disabled by default),
  RTM channel subscription, and subscription to the returned translated-audio publisher.
- A shared bounded PCM playback buffer with independent capture/playback and reference-counted codec ownership.
- ES8311 standard I2S stereo with MCLK256; the Mosaico adapter converts to/from SDK mono PCM.
- A native 480×480 LVGL layout for Mosaico CO5300 QSPI / CST9220 touch; the earlier Korvo keeps its 800×480 layout.
- Native media and face-to-face controls for languages, start/stop, local pause and actual volume;
  The AI key also starts/stops translation. Face-to-face shares one parent session, two RTC connections
  and one RTM channel subscription. Meeting remains a settings preview.
- Localized pairing and Wi-Fi provisioning prompts, with the actual hotspot name shown during
  provisioning.

## Supported Boards

The default `esp-mosaico-v1.2` profile targets 16 MB NOR Flash, 16 MB Octal PSRAM,
ES8311 audio and a 480×480 CO5300 / CST9220 display. It uses `partitions/v2/16m.csv`;
the 128 MB NAND is unused. The earlier `esp32-s31-korvo-1` profile is retained. Video stays disabled.

The user confirmed microphone and speaker operation in the standalone Mosaico UAC test.
The user also confirmed normal caption display with 40-row partial buffers; device logs confirm
touch navigation and start requests. The 16 kHz audio quality, Cloud AEC and endurance need separate results.
See the [board guide](docs/esp-mosaico-v1.2.zh-CN.md) and [validation record](docs/s31-validation.md).

## Build

Install ESP-IDF v6.1 using the
[official setup guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/get-started/index.html),
install `ffmpeg`, then activate ESP-IDF in the current shell:

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
```

Use `build/korvo-s31` and `build/korvo-s31/sdkconfig` for all local firmware builds:

```sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp-mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

CI builds language variants in isolated jobs. The [release guide](docs/RELEASING.md) describes
the packaging builder and delivery checks; local development uses the direct command above.
The direct flash command and board-specific hardware details are in the
[board guide](docs/esp-mosaico-v1.2.zh-CN.md).

The pinned RTSA header and library are bundled under `components/mybot_stack/agora_rtc`, so no
download or import step is required. `ffmpeg` converts the tracked Ogg prompts to 16 kHz mono
S16LE in the build directory.

## Provisioning and Controls

When NVS contains no Wi-Fi credentials, the device creates a configuration AP named
`mybot-xxxx` and serves provisioning at `http://192.168.4.1`. MyBot starts only after the
station has a usable IP address. A provisioning request stops MyBot first and restarts it after
reconnection.

Open Media translation, select input/output languages and use its start/stop controls. Pause
retains the cloud session; empty screen taps have no action. A short Mosaico AI-key press starts/stops
translation, cancels a pending start or retries cleanup. Hold AI for 3 seconds to provision Wi-Fi.
On-screen volume is saved in NVS. Live captions, scrollable full text and session history share RTM text.
See the [translation UI](docs/PLATFORM_UI.md).

Mosaico translation firmware exposes a native Type-C CDC log console. USB audio routing is deferred.
The passed [standalone UAC diagnostic](diagnostics/mosaico_uac/README.zh-CN.md) is preserved for future meeting work.
Korvo-only [CDC](docs/USB_CDC_PROBE.zh-CN.md) and [Host](docs/USB_HOST_PROBE.zh-CN.md) probes remain opt-in.

Use `idf.py --preview -B build/korvo-s31 menuconfig` under `mybot` to select the product language,
initial translation languages, Cloud AEC and AI QoS. The product language selects UI text and
prompts; both builds use `https://mybot.sh3t.agoralab.co/api`. Runtime language selections take
precedence over the initial translation languages.

The bundled RTSA package accepts only 60 ms audio frames. CPU, heap, and playback diagnostics can
be enabled with `CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR=y`; they are disabled by default. See
[buffered audio playback](docs/AUDIO_PLAYBACK.md) for playback behavior and diagnostics.
Application data and worker stacks follow the [PSRAM memory policy](docs/PSRAM_MEMORY.md);
hardware DMA, kernel objects, and the flash-writing main stack retain internal RAM.

## Validation

Current build coverage, recorded real-device results, and unsupported areas are maintained in
the [validation record](docs/s31-validation.md). Local development builds Chinese firmware;
CI covers both languages and verifies link and delivery metadata. The record distinguishes local builds
from remote CI and hardware results. CI builds and host tests do not establish acoustic
quality, panel behavior, power sequencing, Wi-Fi recovery, or long-duration results.

## Documentation

- [Board guide](docs/esp-mosaico-v1.2.zh-CN.md)
- [Translation and flashing](docs/LISTEN_TRANSLATION.zh-CN.md)
- [Validation record](docs/s31-validation.md)
- [Validation index](docs/VALIDATION.md)
- [Board porting](docs/BOARD_PORTING.md)
- [PSRAM memory policy](docs/PSRAM_MEMORY.md)
- [Buffered audio playback](docs/AUDIO_PLAYBACK.md)
- [Platform LVGL UI](docs/PLATFORM_UI.md)
- [Build validation and delivery](docs/RELEASING.md)
- [Contributing](CONTRIBUTING.md)
- [Support](SUPPORT.md)

## License and Dependencies

Project-maintained code is licensed under [Apache-2.0](LICENSE) unless a file carries another
SPDX identifier. Bundled dependencies and media assets have separate licenses or distribution
terms, including AOSL and Agora RTSA. Review [VENDORED_SOURCES.md](VENDORED_SOURCES.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before use or redistribution.
