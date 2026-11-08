# Support

[English](SUPPORT.md) · [简体中文](SUPPORT.zh-CN.md)

Use repository issues for reproducible bugs and focused feature requests. The only
supported target is **ESP-Mosaico CoreBoard V1.2** with ES8311, CO5300/CST9220 and
composite USB audio/CDC. It uses ESP-IDF v6.1 preview and 60 ms RTSA frames. Video
is unavailable.

Include the repository revision, `idf.py --preview --version`, language/defaults,
reproduction steps and sanitized serial logs. State whether the problem occurs
during build, boot, provisioning, pairing, translation start, audio, display or
touch. Include physical board revision, detected Flash/PSRAM and hardware
modifications. Local build reports use `build/mosaico-v1.2` and its `sdkconfig`.

Touch reports should distinguish display output, touch detection and translation
controls. Describe the gesture and screen state. Audio reports should include
`event=audio_bus` logs and identify the scene and failing direction. For meetings,
include the PC input/output selection and USB stream counters. For build reports
include ffmpeg availability and RTSA package version/hash; omit the private archive itself.

HTTP diagnostics print complete bodies and may contain authentication and device data. Remove those values before sharing logs. The [board guide](docs/esp-mosaico-v1.2.zh-CN.md) explains current controls. The [validation record](docs/s31-validation.md) distinguishes the user-passed meeting/caption baseline from later host/build fixes and outstanding quality, AEC, reconnect/endurance and power checks.

This project has no support SLA. Agora SDK licensing and cloud-service issues belong in the applicable Agora support channel.
