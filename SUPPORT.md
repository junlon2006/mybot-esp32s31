# Support

[English](SUPPORT.md) · [简体中文](SUPPORT.zh-CN.md)

Use repository issues for reproducible bugs and focused feature requests. This project supports **ESP32-S31-Korvo-1 V1.1** with ESP-IDF v6.1, ES8389 TDM audio, RGB/GT1151 and 60 ms RTSA frames. Video is unavailable.

Include the repository revision, `idf.py --version`, language/defaults, reproduction steps and sanitized serial logs. State whether the problem occurs during build, boot, provisioning, pairing, conversation start, audio, display or touch. Include physical board revision, detected Flash/PSRAM and hardware modifications.

Touch reports should distinguish display output from touch detection and conversation toggling. Describe the gesture, screen state and whether PLAY works at the same time. Audio reports should include `event=audio_bus` logs and clarify speaker, microphone or full-duplex behavior. For build reports include ffmpeg availability and RTSA package version/hash; omit the private archive itself.

Conversation-start diagnostics print complete bodies and may contain authentication and device data. Remove those values before sharing logs. The [board guide](docs/esp32-s31-korvo-1.en.md) explains controls and the [validation record](docs/s31-validation.md) distinguishes confirmed behavior from outstanding checks.

This project has no support SLA. Agora SDK licensing and cloud-service issues belong in the applicable Agora support channel.
