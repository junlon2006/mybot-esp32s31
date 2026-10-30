# Changelog

## Unreleased — S31 touch and delivery

- Tap the GT1151 screen to start a conversation and tap again to stop through the same public key path as PLAY. Keep the physical PLAY/SET/VOL controls.
- Limit the project to the active ESP32-S31 no-video integration; remove unused S3 profiles, drivers, dependencies and tests while preserving immutable source snapshots.
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
