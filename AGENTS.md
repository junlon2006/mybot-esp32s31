# mybot-esp32s31 agent guide

The only supported hardware target is ESP-Mosaico CoreBoard V1.2 on ESP-IDF v6.1.
The former multi-board selector has been removed. Use `idf.py --preview`.
User instructions take precedence. Preserve unrelated changes; do not commit or push unless asked.

The MyBot SDK under `components/mybot_stack/mybot_sdk/mybot/` is a project-maintained
translation fork. Keep its cloud-AEC MIC/reference PCM contract and 60 ms cadence.
Media uses `v2vt/listen`; face-to-face uses `listen_speak` with two RTC connections
and one RTM login/channel subscription. Mosaico meetings also use listen_speak:
USB capture feeds LISTEN with zero AEC reference; physical MIC feeds SPEAK with the
physical speaker reference. LISTEN plays on the codec; SPEAK returns through USB IN. There is no
assistant compatibility runtime. Keep the supplied RTSA library unchanged.

The separate CMake option `MYBOT_MOSAICO_UAC_TEST` builds only the V1.2 diagnostic
under `diagnostics/mosaico_uac`, in `build/mosaico-uac`. It is not the Mosaico
translation image. Its passed UAC firmware is a separate hardware
baseline; the production Mosaico profile runs media/face-to-face and bidirectional meeting translation
with a 480x480 LVGL UI and one composite UAC2/CDC USB device. Meeting converts USB OUT
48 kHz stereo to SDK 16 kHz mono and interpolates SPEAK PCM back to USB IN. USB IN
transfers follow the host cadence and return silence outside active translated output. See `docs/MOSAICO_USB_AUDIO.zh-CN.md`.

Keep these other source inputs immutable:

- `components/mybot_stack/aosl/` except the component wrapper `CMakeLists.txt` and the
  project-maintained `s31_port/` adapter
- `components/esp_codec_dev/` (official 1.5.9 snapshot)
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
Its MCU TX52/RX40 must not be reversed based on codec DOUT/DIN net names.
Audio lifecycle and bounded playback logic remain separate from the board bus
and codec setup. The public SDK
contract remains 16 kHz mono signed 16-bit PCM and frame counts. Do not invent
register values or claim physical slot/quality validation from host tests.
Capture/playback must remain independent, with shared codec references and bounded
stop. Void destroy transfers ownership; failed teardown stays driver-owned for
later reclamation, never requiring callers to retry an invalid pointer.

Large application buffers/stacks use PSRAM as appropriate; keep ISR/kernel state,
required descriptors and flash-writing stacks in internal memory. Respect S31 DMA
alignment/cache rules. Do not copy S3 DMA assumptions or link Xtensa binaries.
The S31 AOSL task adapter puts audited media, service_mpq (HTTP/JSON) and mybot_rtc
(forwarding) stacks in PSRAM. Keep control_mpq (NVS) and opaque RTSA stacks internal;
creation/deletion must use matching APIs selected from actual stack placement.
The adapter prepares its static internal reaper before admitting PSRAM workers.
Self-exit queues a handle and suspends; the reaper uses IDF's external WithCaps
deletion protocol. Keep its queue, TCB and aligned 2 KiB stack internal; exit must
not allocate a temporary cleanup task.

Do not hand-edit generated sdkconfig/build/managed_components/cache files. Edit
defaults/Kconfig/CMake and regenerate. The user-approved pinned RTSA header/library are versioned immutable inputs.
Original SDK archives, firmware and generated outputs are ignored and must not be committed. Keep credentials/device IDs out of Git.

Before handing off changes, run relevant host lifecycle/fault-injection tests,
clang-format/SPDX and git diff --check; build Chinese firmware with direct idf.py during development.
All local firmware builds use `build/mosaico-v1.2` and `build/mosaico-v1.2/sdkconfig`;
do not create or switch to another local build directory. CI jobs retain isolated
build/sdkconfig directories. The builder supports dry-run; its safe cleanup
requires Python 3.11+, while direct IDF builds support the installed IDF
Python environment. Record binary/config/map checks separately from real hardware
results. Update board docs, validation, source provenance and license notices when
inputs or hardware facts change. The Mosaico diagnostic USB microphone/speaker
have user-reported confirmation. The user also confirmed normal Mosaico 480x480
caption display after changing partial buffers to 40 rows; keep that display baseline. The user
confirmed PC-audio meeting listening plays translated Chinese on the device and reported
bidirectional meeting tests passing after the PSRAM task-stack revision (e279926).
Mosaico media/face-to-face support is implemented. Later review fixes
have host/build results only until another hardware result is recorded.
Translation quality, AEC, reconnect/endurance, USB current and VCC ramp behavior require their own results.
Translation route subscription, language controls, cloud AEC and recovery require
their own hardware results. The development service is https://mybot.sh3t.agoralab.co/api.
