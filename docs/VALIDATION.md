# Validation index

The maintained record for this dedicated S31 project is [s31-validation.md](s31-validation.md). It separates source/host checks, ESP-IDF target builds and user-reported physical-board results.

Hardware feedback confirms the inherited basic bidirectional audio and normal screen display. Translation route subscription, language controls, Cloud AEC, local pause and recovery require separate physical-board results. Build, host-test and delivery results belong in the maintained record, with the tested revision and scope. A local build does not establish remote CI execution or translation behavior on hardware.

The old multi-board S3 matrix is retired with those profiles. Historical baseline changes remain under [archive](archive/esp32-s3-baseline-changelog.md); they do not describe supported S31 hardware.
