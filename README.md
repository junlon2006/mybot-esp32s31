# mybot-esp32s31

[![CI](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml/badge.svg)](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/junlon2006/mybot-esp32s31)](LICENSE)

**[English](README.md) | [简体中文](README.zh-CN.md)**

`mybot-esp32s31` is an ESP-IDF firmware project for standalone V2VT translation on the
**ESP-Mosaico CoreBoard V1.2**, the target for future development. The media scene sends board microphone audio to the cloud and plays
translated speech through the board speaker, initially Chinese to English. Face-to-face supports
alternating Chinese/English speech, with language buttons selecting the microphone route. Mosaico meetings
translate PC audio to the device speaker and device microphone audio back to the PC over USB. The project maintains
the MyBot translation SDK fork and its board, network, audio, display and lifecycle integration.
The development API is `https://mybot.sh3t.agoralab.co/api`.

The development baseline is **ESP-IDF v6.1**. ESP-Mosaico CoreBoard V1.2 is the
only supported board; Flash, Octal PSRAM, the partition table and hardware drivers
are fixed by the project. See the
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
- A native 480×480 LVGL layout for the CO5300 QSPI display and CST9220 touch.
- Native media and face-to-face controls for languages, start/stop, local pause and actual volume.
  Touch is the only translation start/stop input. Face-to-face shares one parent session, two RTC connections
  and one RTM channel subscription. Mosaico meetings translate PC English to the device speaker in Chinese while translating the device microphone to English on the PC USB input.
- Localized pairing and Wi-Fi provisioning prompts, with the actual hotspot name shown during
  provisioning.

## Hardware

The fixed `esp-mosaico-v1.2` profile targets 16 MB NOR Flash, 16 MB Octal PSRAM,
ES8311 audio and a 480×480 CO5300 / CST9220 display. It uses `partitions/v2/16m.csv`;
the 128 MB NAND is unused. Video stays disabled.

The user confirmed basic bidirectional USB meeting translation after the PSRAM-stack revision (`e279926`)
and normal captions with 40-row partial buffers; touch/start requests have device logs.
The standalone UAC microphone/speaker test is a separate passed baseline. Subsequent review fixes
have host-test and Chinese-build results; they have not received a new hardware acceptance.
Audio quality, Cloud AEC, reconnect/endurance, USB current and the revised VCC ramp need separate results.
See the [board guide](docs/esp-mosaico-v1.2.zh-CN.md) and [validation record](docs/s31-validation.md).

## Build

Install ESP-IDF v6.1 using the
[official setup guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/get-started/index.html),
install `ffmpeg`, then activate ESP-IDF in the current shell:

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
```

Build both delivery variants with the same entrypoint used by CI:

```sh
python3 scripts/build_all.py --build-root build/delivery --output-root releases/delivery
```

CI invokes the same script per language with isolated temporary build and release roots.
The script performs reconfigure, build, size, merge, provenance and delivery checks.
For iterative single-board development, keep using `build/mosaico-v1.2`:

```sh
idf.py --preview -B build/mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/mosaico-v1.2/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

The [release guide](docs/RELEASING.md) describes the script's isolated roots, build-bound source
records and delivery files.
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
retains the cloud session; empty screen taps have no action. The Mosaico AI key is a maintenance
control: a press shorter than 3 seconds has no action, holding for 3 seconds stops MyBot and enters
Wi-Fi provisioning, and continuing to hold for 10 seconds erases local NVS and restarts into
provisioning. On-screen volume is saved in NVS. Live captions, scrollable full text and session
history share RTM text. See the [translation UI](docs/PLATFORM_UI.md).

Mosaico translation firmware exposes one native Type-C UAC2/CDC composite device. Meetings
send PC playback to the LISTEN Agent and the physical microphone to the SPEAK Agent concurrently.
LISTEN output plays on the device speaker; SPEAK output returns through the USB microphone.
Boundary filters convert between USB 48 kHz stereo and SDK 16 kHz mono.
See the [USB integration guide](docs/MOSAICO_USB_AUDIO.zh-CN.md).
The passed [standalone UAC diagnostic](diagnostics/mosaico_uac/README.zh-CN.md) remains a separate USB audio reference.

Use `idf.py --preview -B build/mosaico-v1.2 menuconfig` under `mybot` to select the product language,
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
