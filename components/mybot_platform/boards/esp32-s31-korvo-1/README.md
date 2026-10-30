# ESP32-S31-Korvo-1 V1.1

The only supported board ID is `esp32-s31-korvo-1`. Its profile targets ESP-IDF v6.1 preview `esp32s31`, 16 MB Flash/Octal PSRAM, ES8389 TDM, RGB/GT1151 and 60 ms audio. Video is disabled.

- [English guide](../../../../docs/esp32-s31-korvo-1.en.md)
- [中文编译与操作](../../../../docs/esp32-s31-korvo-1.zh-CN.md)
- [Validation](../../../../docs/s31-validation.md)

Tap and release to start/stop a conversation; PLAY provides the same action. SET requests pairing/provisioning and VOL changes saved volume. Screen output and basic voice were user-tested; the new touch behavior is awaiting hardware acceptance.

`reference/esp32_s31_adc_calibration.c` is unchanged Espressif Apache-2.0 source from pinned BSP `bd4d4ac18657e73583af3d7c0b0a23c3cddf3735`; the integration header is maintained locally.
