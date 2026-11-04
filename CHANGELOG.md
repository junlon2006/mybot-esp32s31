# Changelog

## Unreleased — standalone S31 listen translation

- Disable AI QoS by default in firmware and the SDK fallback; retain explicit opt-in support.
- Customize MyBot SDK for one V2VT listen route with persistent source/target selection and a dedicated touch UI.
- Preserve cloud-AEC MIC/reference audio, G.722 and the bundled 60 ms RTSA cadence.
- Subscribe only to the returned translated-audio publisher; retain pending cloud cleanup across cancellation, reconnect and restart.
- Add host protocol, lifecycle, RTC ownership, control mailbox and UI tests. Hardware translation validation is pending.
- Trace request/response bodies for every HTTPS endpoint, with method, redacted path, byte counts, completeness and transport errors.
- Print each captured HTTPS body in one complete log record, including full hex for binary bodies.
- Reject translation starts before UTC is usable, distinguish clock/route/expiry failures, and give translation start a shared 30-second HTTP budget while other requests retain 5 seconds.
- Bootstrap an unset clock from a validated HTTPS response Date header and configure three SNTP alternatives, so initial translation can proceed when NTP has not answered.
- Display the active hardware output volume, including restored and adjusted values, through the same volume source used by the controls.
- Add the native simulator-inspired scene UI, direct language/volume commands, real local media pause/resume, fixed demo captions and preview-only face-to-face/meeting scenes.
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
