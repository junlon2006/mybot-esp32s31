# ESP32-S31-Korvo-1 V1.1

[English](esp32-s31-korvo-1.en.md) · [简体中文](esp32-s31-korvo-1.zh-CN.md)

Board ID: `esp32-s31-korvo-1`. Requires ESP-IDF v6.1 with the S31 preview target and ffmpeg. The approved ESP32-S31 RTSA v1.10.1 build 1366494 payload is bundled. The port includes Wi-Fi provisioning, HTTPS pairing, RTC/RTM, ES8389 voice, PCM prompts, ADC keys and RGB/GT1151 LVGL. Video is unavailable.

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
| Tap the screen | Start a conversation; tap again to stop |
| Short PLAY press | Start/stop through the same conversation path |
| Short SET press | Request pairing again |
| Hold SET for 3 seconds | Stop MyBot, then enter Wi-Fi provisioning |
| VOL+/VOL- | Change and save volume |
| First boot without Wi-Fi | Enter web provisioning with display/prompt guidance |

Wait for usable Wi-Fi, pairing and the ready screen before starting. Touch uses the existing public key event path; display callbacks perform no blocking HTTPS/RTC work. Touch before readiness is ignored. Holding produces one event on release; dragging is not distinguished in this version. A finger already down at initialization/read recovery must be released and pressed again. Dispatch logs use `event=touch action=conversation_start` / `conversation_stop`.

The user confirmed normal physical-board screen output and basic bidirectional voice. The new touch start/stop behavior awaits hardware acceptance: check one toggle per tap, a second tap to stop, and retained PLAY/SET behavior. See [validation](s31-validation.md).

## Hardware and audio

The board has an ESP32-S31-WROOM-3, 16 MB Flash and 16 MB PSRAM. ES8389 and GT1151 share I2C0 SDA0/SCL1. LCD is 800×480 RGB565 at 26 MHz with two PSRAM framebuffers. Touch reset/IRQ and display/backlight controls are NC. ADC keys use GPIO42 and the pinned official calibration source.

Audio follows the official guide's TDM requirement: 16 kHz, four 16-bit slots, 64 BCLK/frame, BCLK3/WS4/TX5/RX6, PA7. The codec takes BCLK with `use_mclk=false` and `no_dac_ref=false`; the CPU outputs no MCLK. The adapter checks STDM registers, maps playback slots and selects a microphone slot. The public SDK contract remains 16 kHz mono signed-16 PCM and 60 ms frames. Nominal BCLK/WS are 1.024 MHz/16 kHz; these are configured values, not instrument measurements.

The pinned factory BSP uses standard stereo I2S; this port deliberately follows the official TDM requirement. Basic voice interoperability does not establish detailed microphone/reference slot mapping, acoustic quality or endurance. Local AEC, beamforming, wake word, video, SD/USB application paths and low-power work remain outside this port. The current update covers touch, CI/delivery and repository cleanup only.

## Conversation-start diagnostics

`POST .../conversations/start` prints complete request/response bodies, including HTTP errors. Filter `event=http_body` by connection `id` and `direction`. Begin/end records show size and completeness; data records carry ordered offsets and bounded UTF-8 chunks. NUL-containing bodies use hex. Remove authentication and device data before sharing logs.

## References

Hardware facts and unchanged ADC calibration source come from the [official guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html) and [pinned BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo). Factory application code/assets are not prerequisites. See root provenance and license notices for exact inputs.
