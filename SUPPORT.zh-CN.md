# 支持

[English](SUPPORT.md) · [简体中文](SUPPORT.zh-CN.md)

可复现问题与明确特性请求请提交仓库 issue。当前仅支持 **ESP32-S31-Korvo-1 V1.1**，使用 ESP-IDF v6.1、ES8389 TDM、RGB/GT1151 和 60 ms RTSA 音频帧；视频不可用。

提供仓库版本、`idf.py --version`、语言/defaults、复现步骤和脱敏日志。说明发生阶段：构建、启动、配网、配对、启动对话、音频、显示或触摸；注明实体板版本、检测到的 Flash/PSRAM 和硬件修改。

触摸问题请区分屏幕显示、触点识别与开启/结束对话，说明手势、界面状态和 PLAY 是否正常。音频问题附 `event=audio_bus` 日志，明确是扬声器、麦克风还是双向并发问题。构建问题说明 ffmpeg 及 RTSA 包版本/hash，无需上传私有归档。

开启对话的完整正文日志可能包含认证和设备信息，分享前请删除这些值。[板卡文档](docs/esp32-s31-korvo-1.zh-CN.md)说明操作，[验证记录](docs/s31-validation.md)区分已确认结果与待测项目。

本项目不提供支持 SLA。Agora SDK 授权或云服务问题请联系对应 Agora 支持渠道。
