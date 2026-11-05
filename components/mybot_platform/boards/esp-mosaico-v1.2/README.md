<!-- SPDX-License-Identifier: Apache-2.0 -->
# ESP-Mosaico CoreBoard V1.2

The production board selector is `MYBOT_BOARD=esp-mosaico-v1.2`. This board uses
standard Philips I2S with two 16-bit slots and MCLK at 256 × 16 kHz. The SDK still
reads and writes 16 kHz mono signed 16-bit PCM; the adapter takes RX slot zero and
copies output PCM into both TX slots. Cloud AEC uses the SDK's playback reference.
The ES8311 DAC loopback is disabled.

Board wiring follows the official CoreBoard V1.2 schematic and the Mosaico BSP
at `8ce87a7ef130f946de4f84f6a5a20db1d8982605`. Power and audio pin assignments are
shared with the separately tested `diagnostics/mosaico_uac` image. That diagnostic's
48 kHz USB audio result does not establish this firmware's 16 kHz translation,
cloud AEC, display or touch behavior.

- I2C0: SDA 56, SCL 3; owned by the board and shared by ES8311 and touch.
- I2S0: MCU TX 52, RX 40, BCLK 37, WS 49, MCLK 54. PA 45 is active high.
- Power: VCC enable 60 is active low; shutdown 57 is released open-drain high.
- AI key: GPIO 7 active low; click starts/stops, hold three seconds provisions Wi-Fi.
- Display: 480 × 480 CO5300 QSPI; CST9220 touch.

`src/drivers/audio/codec_audio.c` contains the pre-existing MIT-licensed Korvo
stream lifecycle, bounded I/O, deferred destruction and volume persistence code.
Its board-specific I2S/codec setup is supplied by `mosaico_codec_audio.c` or
`korvo_s31_codec_audio.c`; the shared lifetime code is not duplicated.
`mosaico_codec_audio.c` and the power pin setup use Apache-2.0-licensed Espressif
configuration patterns. The existing official `esp_codec_dev` 1.5.9 snapshot,
including its Apache-2.0 ES8311 driver, is unchanged.

Host validation covers both audio backends using `tests/korvo_s31_audio/run.sh`.
The shared input suite also checks the Mosaico key's debounce, boot-held rejection,
long-press provisioning, detached callback handling and UI command validity.
