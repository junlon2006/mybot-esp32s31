# mybot-esp32s31 agent guide

The default and future design target is ESP-Mosaico CoreBoard V1.2 on ESP-IDF v6.1,
selected by `MYBOT_BOARD=esp-mosaico-v1.2`. The earlier `esp32-s31-korvo-1` profile
is retained for existing devices. Use `idf.py --preview`.
User instructions take precedence. Preserve unrelated changes; do not commit or push unless asked.

The MyBot SDK under `components/mybot_stack/mybot_sdk/mybot/` is a project-maintained
translation fork. Keep its cloud-AEC MIC/reference PCM contract and 60 ms cadence.
Media uses `v2vt/listen`; face-to-face uses `listen_speak` with two RTC connections
and one RTM login/channel subscription. Meeting remains a UI preview. There is no
assistant compatibility runtime. Keep the supplied RTSA library unchanged.

An optional `MYBOT_USB_CDC_PROBE` diagnostic uses native USB Device CDC and leaves
UART0 as the console/flash path. It is off by default; USB audio/meeting routing
is not implemented. CDC diagnostic boots skip NVS, network, display, audio and
translation startup. J2's host VBUS is hardwired on: firmware does not control or
validate its power connection. See `docs/USB_CDC_PROBE.zh-CN.md` for its scope.
The mutually exclusive `MYBOT_USB_HOST_PROBE` runs a diagnostic-only boot with a
direct USB peripheral on J2. It prints descriptors without accessing files; no
network, display, audio or translation starts. See `docs/USB_HOST_PROBE.zh-CN.md`.
The separate CMake option `MYBOT_MOSAICO_UAC_TEST` builds only the V1.2 diagnostic
under `diagnostics/mosaico_uac`, still in `build/korvo-s31`. It is not a Korvo image
or the Mosaico translation image. Its passed UAC firmware is a separate hardware
baseline; the production Mosaico profile currently runs media/face-to-face translation
with a 480x480 LVGL UI and a USB CDC log console, without USB audio routing.

Keep these other source inputs immutable:

- `components/mybot_stack/aosl/` except the component wrapper `CMakeLists.txt` and the
  project-maintained `s31_port/` adapter
- `components/esp_codec_dev/` (official 1.5.9 snapshot)
- the official ADC reference source under the S31 board's `reference/`
- supplied RTSA header/library and external reference repositories

Adapt SDK CMake wrappers, `components/mybot_platform`, board metadata, main, CI,
scripts and documentation. The runtime components `mybot_sdk`, `aosl` and `agora_rtc`
live directly under `components/mybot_stack`; register only one implementation each.
Only local source-built AOSL uses WHOLE_ARCHIVE. Keep RTSA a normal archive and
inspect the final map to reject host AOSL objects. Include MyBot only via public headers.

The first firmware intentionally has no video. Do not enable camera/JPEG dependencies
without an explicit new request. Display boards use the shared LVGL view automatically.
Shared I2C0 belongs to the board; codec/touch adapters must not delete it.

Mosaico uses ES8311, 16 kHz standard Philips I2S, two 16-bit slots and MCLK256.
Its MCU TX52/RX40 must not be reversed based on codec DOUT/DIN net names. Korvo
retains ES8389 TDM4, 64 BCLK/frame and no MCLK output. Both share audio lifecycle
and bounded playback logic while keeping board-specific bus/codec setup. The public SDK
contract remains 16 kHz mono signed 16-bit PCM and frame counts. Do not invent
register values or claim physical slot/quality validation from host tests.
Capture/playback must remain independent, with shared codec references and bounded
stop. Void destroy transfers ownership; failed teardown stays driver-owned for
later reclamation, never requiring callers to retry an invalid pointer.

Large application buffers/stacks use PSRAM as appropriate; keep ISR/kernel state,
required descriptors and flash-writing stacks in internal memory. Respect S31 DMA
alignment/cache rules. Do not copy S3 DMA assumptions or link Xtensa binaries.

Do not hand-edit generated sdkconfig/build/managed_components/cache files. Edit
defaults/Kconfig/CMake and regenerate. The user-approved pinned RTSA header/library are versioned immutable inputs.
Original SDK archives, firmware and generated outputs are ignored and must not be committed. Keep credentials/device IDs out of Git.

Before handing off changes, run relevant host lifecycle/fault-injection tests,
clang-format/SPDX and git diff --check; build Chinese firmware with direct idf.py during development.
All local firmware builds use `build/korvo-s31` and `build/korvo-s31/sdkconfig`;
do not create or switch to another local build directory. CI jobs retain isolated
build/sdkconfig directories. The builder supports dry-run; its safe cleanup
requires Python 3.11+, while direct IDF builds support the installed IDF
Python environment. Record binary/config/map checks separately from real hardware
results. Update board docs, validation, source provenance and license notices when
inputs or hardware facts change. The Korvo media/face-to-face baseline and Mosaico diagnostic USB microphone/speaker
have user-reported confirmation. The user also confirmed normal Mosaico 480x480
caption display after changing partial buffers to 40 rows; keep that display baseline.
Translation audio quality, AEC and long-duration behavior require their own results.
Translation route subscription, language controls, cloud AEC and recovery require
their own hardware results. The development service is https://mybot.sh3t.agoralab.co/api.
