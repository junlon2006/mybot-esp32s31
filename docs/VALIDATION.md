# Validation index

The maintained record for this dedicated S31 project is [s31-validation.md](s31-validation.md). It separates source/host checks, ESP-IDF target builds and user-reported physical-board results.

User feedback confirms standalone USB recording/playback, normal 40-row caption
display and basic bidirectional meeting translation after the PSRAM-stack revision
(`e279926`). Touch/start requests also have device logs. Later review fixes have
host/build results only. Detailed subscription/language/pause/recovery behavior,
acoustic quality, Cloud AEC, reconnect/endurance, USB current and VCC ramp timing
require separate hardware results. Build, host-test and delivery results belong in
the maintained record, with the tested revision and scope. A local build does not
establish remote CI execution or translation behavior on hardware.

The old multi-board S3 matrix and removed S31 profiles are retired. Historical
baseline changes remain under [archive](archive/esp32-s3-baseline-changelog.md);
they do not describe supported hardware.
