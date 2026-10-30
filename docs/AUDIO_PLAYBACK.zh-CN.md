# S31 音频播放

[English](AUDIO_PLAYBACK.md) · [简体中文](AUDIO_PLAYBACK.zh-CN.md)

MyBot 公开音频契约为 16 kHz、mono、signed 16-bit PCM，长度使用帧数。ES8389 adapter 将其映射到四个 16-bit Philips TDM 时隙。采集/播放独立持有 handle，共享 codec/I2S 引用计数，PA 跟随播放。

共享 PCM buffer 的 FIFO、暂存数据和 worker 栈使用 PSRAM，同步控制留在内部内存。owner 提供有界 sink write，等待 worker 确认后才释放资源，stop 超时保留待清理资源。有限提示音播放完成时等待 FIFO 和 DMA 尾部排空再停止。

受跟踪的 Ogg 提示音/数字是构建输入：CMake 调用 ffmpeg 在当前构建目录生成 16 kHz mono S16LE，再嵌入固件。固件不运行 Ogg/Opus 解码，也不链接 S3 decoder。移除运行时解码器后仍保留源素材、hash 和许可。

S31 RTSA 使用 **60 ms** jitter-buffer 帧，区别于源 Ogg 历史编码的 20 ms Opus 包，也区别于驱动 sink 分块。提供匹配 RTSA 之前，20/40 ms 配置会被拒绝。

用户已真机确认基础双向语音。详细参考/麦克风路由、量化音质、回声调优和长稳不在本轮范围。host 测试位于 `tests/korvo_s31_audio`、`tests/pcm_playback_buffer`，结果见[验证记录](s31-validation.md)。总线引脚与操作见[板卡文档](esp32-s31-korvo-1.zh-CN.md)。
