# mybot-esp32s31

[![CI](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml/badge.svg)](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/junlon2006/mybot-esp32s31)](LICENSE)

**[English](README.md) | [简体中文](README.zh-CN.md)**

`mybot-esp32s31` is an ESP-IDF firmware project for standalone V2VT translation on the
ESP32-S31-Korvo-1 V1.1 board. The media scene sends board microphone audio to the cloud and plays
translated speech through the board speaker, initially Chinese to English. Face-to-face supports
alternating Chinese/English speech, with language buttons selecting the microphone route. The project maintains
the MyBot translation SDK fork and its board, network, audio, display and lifecycle integration.
The development API is `https://mybot.sh3t.agoralab.co/api`.

The development baseline is **ESP-IDF v6.1**. The only supported board profile is
`esp32-s31-korvo-1`; Flash, Octal PSRAM, the partition table, and hardware drivers are selected
with `MYBOT_BOARD`. See the
[board guide](docs/esp32-s31-korvo-1.en.md) for hardware details, controls, build commands,
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
- A shared bounded PCM playback buffer and independent TDM writer on the supported board.
- ES8389 audio with four 16-bit Philips TDM slots, independent capture/playback attachment,
  and reference-counted codec/I2S ownership.
- Automatic LVGL on the 800 x 480 RGB565 display with GT1151 touch.
- Native media and face-to-face controls for languages, start/stop, local pause and actual volume;
  PLAY also starts/stops translation. Face-to-face shares one parent session, two RTC connections
  and one RTM channel subscription. Meeting remains a settings preview.
- Localized pairing and Wi-Fi provisioning prompts, with the actual hotspot name shown during
  provisioning.

## Supported Boards

The only supported profile is `esp32-s31-korvo-1`.

It targets the ESP32-S31-WROOM-3 with 16 MB Flash and 16 MB Octal PSRAM, an ES8389 codec, an
800 x 480 RGB565 panel, GT1151 touch, and GPIO42 ADC keys. The firmware uses
`partitions/v2/16m.csv`. Video is intentionally unavailable.

The user has confirmed the inherited basic bidirectional audio and normal screen output on the
physical board. Translation route subscription, language controls, Cloud AEC and recovery still
require their own hardware results. See the
[board guide](docs/esp32-s31-korvo-1.en.md) for controls and the
[validation record](docs/s31-validation.md) for the scope of each result.

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
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

CI builds language variants in isolated jobs. The [release guide](docs/RELEASING.md) describes
the packaging builder and delivery checks; local development uses the direct command above.
The direct flash command and board-specific hardware details are in the
[board guide](docs/esp32-s31-korvo-1.en.md).

The pinned RTSA header and library are bundled under `components/mybot_stack/agora_rtc`, so no
download or import step is required. `ffmpeg` converts the tracked Ogg prompts to 16 kHz mono
S16LE in the build directory.

## Provisioning and Controls

When NVS contains no Wi-Fi credentials, the device creates a configuration AP named
`mybot-xxxx` and serves provisioning at `http://192.168.4.1`. MyBot starts only after the
station has a usable IP address. A provisioning request stops MyBot first and restarts it after
reconnection.

Open Media translation, select input/output languages and use its start/stop controls. Pause
retains the cloud session; empty screen taps have no action. A short PLAY press starts/stops
translation, cancels a pending start or retries cleanup. SET requests pairing, a 3-second SET hold
requests provisioning, and VOL+/VOL- change and persist volume in NVS. Live captions, scrollable
full text and session history share RTM original/translated text; brightness remains a demo.
See the [translation UI](docs/PLATFORM_UI.md).

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

- [Board guide](docs/esp32-s31-korvo-1.en.md)
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
