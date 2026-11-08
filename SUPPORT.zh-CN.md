# 支持

[English](SUPPORT.md) · [简体中文](SUPPORT.zh-CN.md)

可复现问题与明确特性请求请提交仓库 issue。仅支持 **ESP-Mosaico CoreBoard V1.2**，
使用 ES8311、CO5300／CST9220 和复合 USB 音频／CDC。使用 ESP-IDF v6.1 preview
和 60 ms RTSA 音频帧；视频不可用。

提供仓库版本、`idf.py --preview --version`、语言／defaults、复现步骤和脱敏日志。
说明发生阶段：构建、启动、配网、配对、启动翻译、音频、显示或触摸；注明实体板版本、
检测到的 Flash／PSRAM 和硬件修改。本地构建使用 `build/mosaico-v1.2` 及其中的 `sdkconfig`。

触摸问题请区分屏幕显示、触点识别与翻译控件，说明手势和界面状态。音频问题附
`event=audio_bus` 日志，明确场景及故障方向；会议问题同时说明电脑输入／输出选择和
USB 收发计数。构建问题说明 ffmpeg 及 RTSA 包版本／hash，无需上传私有归档。

HTTP 完整正文日志可能包含认证和设备信息，分享前请删除这些值。[板卡文档](docs/esp-mosaico-v1.2.zh-CN.md)说明当前操作。[验证记录](docs/s31-validation.md)区分用户通过的会议／字幕基线、后续 host／构建修复，以及尚待验证的音质、AEC、重连／长稳与供电项目。

本项目不提供支持 SLA。Agora SDK 授权或云服务问题请联系对应 Agora 支持渠道。
