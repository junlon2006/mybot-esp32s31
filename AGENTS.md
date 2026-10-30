# mybot-esp32s31 agent guide

This repository targets ESP32-S31-Korvo-1 V1.1 on ESP-IDF v6.1. The only registered
board is `esp32-s31-korvo-1`; `MYBOT_BOARD` is the board selector. Use `idf.py --preview`.
User instructions take precedence. Preserve unrelated changes; do not commit or push unless asked.

Keep these inputs immutable:

- `components/mybot_stack/mybot_sdk/mybot/`
- `components/mybot_stack/aosl/`
- `components/esp_codec_dev/` (official 1.5.9 snapshot)
- the official ADC reference source under the S31 board's `reference/`
- supplied RTSA header/library and external reference repositories

Adapt SDK CMake wrappers, `components/mybot_platform`, board metadata, main, CI,
scripts and documentation. The external `ports/aosl` and `ports/agora_rtc` wrappers
retain component names `aosl` and `agora_rtc`; register only one implementation each.
Only local source-built AOSL uses WHOLE_ARCHIVE. Keep RTSA a normal archive and
inspect the final map to reject host AOSL objects. Include MyBot only via public headers.

The first firmware intentionally has no video. Do not enable camera/JPEG dependencies
without an explicit new request. Display boards use the shared LVGL view automatically.
Shared I2C0 belongs to the board; codec/touch adapters must not delete it.

Audio follows the TDM requirement: the initial controller uses 16 kHz, four 16-bit
slots, 64 BCLK/frame, native Philips framing and no MCLK output. The public SDK
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
clang-format/SPDX and git diff --check; build both languages with direct idf.py.
Use independent build/sdkconfig directories. The builder supports dry-run; its safe
cleanup requires Python 3.11+, while direct IDF builds support the installed IDF
Python environment. Record binary/config/map checks separately from real hardware
results. Update board docs, validation, source provenance and license notices when
inputs or hardware facts change. Basic bidirectional audio and the display have user-reported hardware confirmation.
Touch toggling and CI delivery changes still require their own hardware/remote results.
