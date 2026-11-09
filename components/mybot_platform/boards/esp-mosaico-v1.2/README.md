<!-- SPDX-License-Identifier: Apache-2.0 -->
# ESP-Mosaico CoreBoard V1.2

This is the only supported board. It uses
standard Philips I2S with two 16-bit slots and MCLK at 256 × 16 kHz. The SDK still
reads and writes 16 kHz mono signed 16-bit PCM; the adapter copies output PCM into
both TX slots. Local AEC, enabled by default, uses RX slot zero for the microphone
and ES8311 DAC reference in slot one, then returns ESP-SR-cleaned mono PCM. Selecting
Cloud AEC keeps the legacy slot-zero microphone and SDK playback-reference path.

Board wiring follows the official CoreBoard V1.2 schematic and the Mosaico BSP
at `8ce87a7ef130f946de4f84f6a5a20db1d8982605`. Power and audio pin assignments are
shared with the separately tested `diagnostics/mosaico_uac` image. Separate user feedback
confirms normal 40-row caption display and basic bidirectional meeting translation
after the PSRAM-stack revision (`e279926`); touch/start requests have device logs.
Later recovery, identity, power and local-AEC changes have host/build coverage only.
Acoustic AEC quality, reconnect/endurance, current and VCC ramp timing still need hardware results.

- I2C0: SDA 56, SCL 3; owned by the board and shared by ES8311 and touch.
- I2S0: MCU TX 52, RX 40, BCLK 37, WS 49, MCLK 54. PA 45 is active high.
- Power: VCC enable 60 is active low; shutdown 57 is released open-drain high.
- AI key: GPIO 7 active low; presses shorter than three seconds do nothing, three seconds enters
  Wi-Fi provisioning, and ten seconds erases local NVS and restarts into provisioning.
- Display: 480 × 480 CO5300 QSPI; CST9220 touch.

`src/drivers/audio/codec_audio.c` contains the shared stream lifecycle, bounded
I/O, deferred destruction and volume persistence code. Board-specific I2S/codec
setup is supplied by `mosaico_codec_audio.c`.
`mosaico_codec_audio.c` and the power pin setup use Apache-2.0-licensed Espressif
configuration patterns. The existing official `esp_codec_dev` 1.5.9 snapshot,
including its Apache-2.0 ES8311 driver, is unchanged. Production defaults compile
only the ES8311 driver; the other retained snapshot drivers are explicitly disabled.

Host validation covers the Mosaico audio backend using `tests/codec_audio/run.sh`.
The input suite also checks the Mosaico key's debounce, boot-held rejection,
short-press no-op, three-second provisioning, ten-second factory reset, detached callback
handling and UI command validity.
