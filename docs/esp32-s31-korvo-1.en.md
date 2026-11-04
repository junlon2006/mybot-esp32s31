# ESP32-S31-Korvo-1 V1.1

[English](esp32-s31-korvo-1.en.md) · [简体中文](esp32-s31-korvo-1.zh-CN.md)

Board ID: `esp32-s31-korvo-1`. Requires ESP-IDF v6.1 with the S31 preview target and ffmpeg. The approved ESP32-S31 RTSA v1.10.1 build 1366494 payload is bundled. The application provides standalone board MIC → cloud `v2vt/listen` translation → board speaker, with Wi-Fi provisioning, HTTPS pairing, RTC/RTM, PCM prompts, ADC keys and RGB/GT1151 LVGL. The development API is `https://mybot.sh3t.agoralab.co/api`. Video is unavailable. See [translation and flashing](LISTEN_TRANSLATION.zh-CN.md).

## Build and flash

Build directly with the bundled pinned RTSA dependency:

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/korvo-s31-en \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31-en/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults;ci/en-us.defaults" build
idf.py --preview -B build/korvo-s31-en -p PORT flash monitor
```

Development uses `idf.py` directly. Use the onboard USB-UART bridge; the separate power connector carries no serial data. UART0 uses TX58/RX59. Chinese omits `ci/en-us.defaults` and uses a separate build directory/sdkconfig. Reconfigure and inspect generated configuration after changing defaults.

ffmpeg converts tracked Ogg prompts to 16 kHz mono S16LE in the build directory. The firmware links no S3 Opus decoder. See [delivery](RELEASING.md) for merged images and metadata. Regular `idf.py flash` preserves NVS; a merged image at **0x0** overwrites the NVS gap and requires provisioning again.

## Controls

| Action | Result |
| --- | --- |
| Media translation controls | Select languages, start, cancel setup, stop or retry cleanup |
| Pause/resume | Pause local media while retaining the RTC parent session |
| Volume drag | Commit once on release and display the actual output volume |
| Short PLAY press | Start/stop translation, cancel a pending start or retry cleanup |
| Short SET press | Request pairing again |
| Hold SET for 3 seconds | Stop MyBot, then enter Wi-Fi provisioning |
| VOL+/VOL- | Change and save volume |
| First boot without Wi-Fi | Enter web provisioning with display/prompt guidance |

After Wi-Fi and pairing, open Media translation from the home screen. Use the language and start/stop controls; empty screen taps have no action. Face-to-face and meeting modes provide settings previews; brightness remains a demo. Media captions, scrollable full text and session history receive original/translated RTM text from the current session. GUI callbacks enqueue copied commands and perform no blocking HTTPS/RTC work. Volume dragging commits only on valid release. Read errors, stale SDK epochs and invalid states discard gestures. See [the scene UI](PLATFORM_UI.md).

The user confirmed normal physical-board screen output and basic bidirectional voice. The new scene UI, parameter controls and local pause/resume await hardware acceptance, including retained PLAY/SET behavior. See [validation](s31-validation.md).

## Hardware and audio

The board has an ESP32-S31-WROOM-3, 16 MB Flash and 16 MB PSRAM. ES8389 and GT1151 share I2C0 SDA0/SCL1. LCD is 800×480 RGB565 at 26 MHz with two PSRAM framebuffers. Touch reset/IRQ and display/backlight controls are NC. ADC keys use GPIO42 and the pinned official calibration source.

Audio follows the official guide's TDM requirement: 16 kHz, four 16-bit slots, 64 BCLK/frame, BCLK3/WS4/TX5/RX6, PA7. The codec takes BCLK with `use_mclk=false` and `no_dac_ref=false`; the CPU outputs no MCLK. The adapter checks STDM registers, maps playback slots and selects a microphone slot. The public SDK contract remains 16 kHz mono signed-16 PCM and 60 ms frames. Nominal BCLK/WS are 1.024 MHz/16 kHz; these are configured values, not instrument measurements.

The pinned factory BSP uses standard stereo I2S; this port follows the official TDM requirement. Basic voice interoperability does not establish detailed microphone/reference slot mapping, acoustic quality, translation behavior or endurance. Local AEC, beamforming, wake word, video, SD/USB application paths and low-power work remain outside this port. Media translation retains the board microphone/speaker path; route subscription, Cloud AEC, language controls, local pause and recovery need their own hardware acceptance.

## Conversation-start diagnostics

All HTTPS requests print captured request/response bodies, including pairing, binding-status, translation start/stop and HTTP errors. Filter `event=http_body` by connection `id` and `direction`. Each body is emitted in full in one `phase=data` record; empty bodies emit `data=`. Begin/end records retain length and completeness, and begin records include method, redacted path, status, transport-error state and accepted/captured wire-byte counts. Partial or oversized bodies are marked explicitly. Capture is bounded to 2 KiB per request and 32 KiB per response, including headers. NUL-containing bodies use one complete hex record backed by temporary PSRAM; allocation failure marks body output unavailable. Headers are not printed. Remove authentication and device data from bodies before sharing logs. Translation start requires a usable UTC clock and has a shared 30-second HTTP budget; other requests retain 5 seconds. Three SNTP servers provide alternatives; an already verified HTTPS 2xx Date header can initialize an unset clock, with `event=time_sync source=https_date result=ok`. It does not overwrite an initialized clock or change TLS settings.

## References

Hardware facts and unchanged ADC calibration source come from the [official guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html) and [pinned BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo). Factory application code/assets are not prerequisites. See root provenance and license notices for exact inputs.
