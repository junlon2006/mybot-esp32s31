# Changelog

## Unreleased — standalone S31 translation

- Simplify the automation builder to registered S31 profiles, default to Mosaico, and remove unused video/S3 fallbacks.

- Remove the unused emoji arrays, PNG inputs and generator while retaining the fonts used by the translation UI.

- Align build/release instructions, board capabilities and validation status with the current Mosaico target and fixed local build directory.

- Enforce at least 300 KiB application-image headroom against the built OTA partition capacities during local and CI delivery checks.

- Enable fail-fast UBSan by default for USB audio host tests and explicitly in CI, and document concurrency validation limits.

- Check Mosaico's eFuse board revision before pin setup and use the pinned BSP's VCC soft-start sequence.

- Declare Mosaico's explicit 500 mA USB 2.0 bus-powered budget in both USB speeds, with measured-load validation kept separate.

- Reclaim PSRAM AOSL task stacks through a static internal reaper, avoiding allocation of temporary cleanup tasks during worker exit.

- Bind delivery provenance to the source captured during the firmware build and verify its artifact hashes when packaging.

- Advertise USB meeting audio only after the USB audio core initializes successfully, independently of console availability or cable state.

- Derive the composite USB serial number from the hardware MAC, preserving stable device identity across reconnects.

- Discard stale AI/PLAY button releases after a session or SDK attachment changes, preventing accidental restarts.

- Preserve the selected language pair when switching between media, face-to-face and USB meeting scenes.

- Keep application ownership until physical/USB media, HTTP and RTC teardown completes; repeated stop retries retained resources before allowing restart.

- Recover the physical playback backend after a hard audio fault so later translation sessions can play again.

- Preserve an unknown start result after ambiguous gateway/server failures, preventing duplicate cloud sessions until recovery.

- Preserve NVS on initialization failures instead of automatically erasing credentials and pending session journals.

- Move audited media, HTTP and RTC-forwarding task stacks to PSRAM through the S31 adapter, reducing duplex internal-stack demand by about 120 KiB. Log task allocation failures and prevent unsafe RTSA initialization retries after a vendor failure.

- Add simultaneous meeting SPEAK translation from the board microphone to the PC USB input, alongside the existing LISTEN-to-speaker path. Use one parent session, two RTC routes, one RTM subscription, separate AEC references and host-paced USB microphone packets.

- Add Mosaico meeting listening: USB PC audio is filtered from 48 kHz stereo to 16 kHz mono, translated by the listen Agent, and played on the board speaker. Use owned USB receive buffering, bounded capture lifecycle and zero digital-input AEC reference; USB microphone return remains silent.

- Attach composite USB interposition directly to the final ELF so transfer counters and initial feedback correction execute; verify live wrappers and application link flags in delivery checks.

- Add one UAC2/CDC composite USB device to Mosaico firmware for meeting step 1: silent USB capture, discarded PC playback, and periodic transfer statistics alongside existing translation.

- Use 40-row Mosaico display buffers; the user confirmed that the persistent caption stripe disappeared and display output is normal.

- Store compact session journals to fit atomic updates in the existing NVS partition; preserve legacy journal recovery and all existing Wi-Fi/binding data.

- Expose exact NVS operation errors, storage capacity and heap diagnostics when translation persistence fails; distinguish journal allocation from write failures without logging stored values.

- Respect Mosaico rounded screen corners and read CST9220 reports only when ready, preventing repeated ACK reads from cancelling touches.

- Make ESP-Mosaico CoreBoard V1.2 the default and future design target, retaining the Korvo profile.
- Port media and face-to-face translation to ES8311 stereo I2S, AI-key controls and a native 480×480 CO5300/CST9220 LVGL layout; expose a Type-C CDC log console.
- Share the existing bounded audio lifecycle across board-specific codec backends, and validate both PCM mappings and square-screen controls in host tests. Mosaico translation hardware acceptance remains pending.
- Add an opt-in S31 native USB CDC echo diagnostic, pinned TinyUSB dependencies and a bounded macOS binary-integrity checker; USB audio remains pending.
- Add a separate USB Host descriptor probe for direct Android-phone/peripheral enumeration, without starting translation or accessing files.
- Disable AI QoS by default in firmware and the SDK fallback; retain explicit opt-in support.
- Add face-to-face translation with two RTC routes, one shared RTM subscription, explicit microphone direction selection and direction-aware captions/history.
- Receive trusted RTM original/translated captions, merge streaming revisions and share a bounded session history across the live/full-text/history pages.
- Add a common Chinese/Latin-1 caption font with real 2 bpp glyphs and bounded PSRAM text storage.
- Customize MyBot SDK for V2VT media/listen and face-to-face/listen_speak with persistent language selection and a dedicated touch UI.
- Preserve cloud-AEC MIC/reference audio, G.722 and the bundled 60 ms RTSA cadence.
- Subscribe only to the returned translated-audio publisher; retain pending cloud cleanup across cancellation, reconnect and restart.
- Add host protocol, lifecycle, RTC ownership, control mailbox and UI tests. Hardware translation validation is pending.
- Trace request/response bodies for every HTTPS endpoint, with method, redacted path, byte counts, completeness and transport errors.
- Print each captured HTTPS body in one complete log record, including full hex for binary bodies.
- Reject translation starts before UTC is usable, distinguish clock/route/expiry failures, and give translation start a shared 30-second HTTP budget while other requests retain 5 seconds.
- Bootstrap an unset clock from a validated HTTPS response Date header and configure three SNTP alternatives, so initial translation can proceed when NTP has not answered.
- Display the active hardware output volume, including restored and adjusted values, through the same volume source used by the controls.
- Add the native simulator-inspired scene UI, direct language/volume commands, real local media pause/resume and a preview-only meeting scene.
- Clear temporary cleanup status after confirmed session stop and show completion instead of a stale startup-error page, while retaining genuine failures.
- Finalize RTC after each translation session so another Bot project's App ID can be used; block cloud starts when local RTC cleanup is incomplete.
- Keep language selection locked until its submitted pair is read back, and correct volume-persistence failure log arguments.

- Retain physical PLAY/SET/VOL controls alongside the native translation UI.
- Limit the project to the active ESP32-S31 no-video integration; remove unused S3 profiles, drivers, dependencies and tests while preserving pinned third-party inputs.
- Add checked S31 delivery metadata and language-specific artifacts, with push/PR firmware CI using the bundled pinned RTSA dependency.
- Align the runtime layout with the ESP32 project by placing `mybot_sdk`, `aosl` and `agora_rtc` directly under `components/mybot_stack`, removing the platform-local ports directory.
- Update Chinese/English build, board, contribution, support, architecture and delivery documentation.
- Record the user's successful physical-board display test separately from touch interaction, which awaits hardware acceptance.

## Conversation-start HTTPS diagnostics — 2026-10-30

- Print complete conversation-start request/response bodies, including HTTP errors, using per-connection IDs and ordered chunks.
- Handle fragmented/partial TLS I/O and chunked responses in the platform transport without changing the immutable MyBot SDK.
- Record the user's successful bidirectional voice test, HTTP 201 conversation start and normal RTC join/leave.

## ESP32-S31 first test firmware — 2026-10-30

- Import MyBot baseline `2782cc88f52c8a46419654b72a918a8273cc23bf`; add board `esp32-s31-korvo-1`, IDF v6.1 and source-built RISC-V AOSL.
- Use the supplied S31 RTSA v1.10.1 build 1366494 and fixed 60 ms frames.
- Add ES8389 16 kHz TDM4×16-bit, independent capture/playback ownership and persisted volume.
- Convert prompt assets to PCM at build time; add ADC keys, RGB/LVGL and GT1151 hardware integration.
- Keep video disabled; record local language builds and host fault-injection validation independently of later hardware feedback.

Inherited ESP32-S3 history is preserved in [the archived baseline changelog](docs/archive/esp32-s3-baseline-changelog.md). It does not describe the supported board set of this project.
