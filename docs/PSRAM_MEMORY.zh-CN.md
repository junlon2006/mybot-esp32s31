# S31 内存策略

[English](PSRAM_MEMORY.md) · [简体中文](PSRAM_MEMORY.zh-CN.md)

板级启用 Octal PSRAM；target defaults 选择 CPU 320 MHz、PSRAM 200 MHz、普通 malloc 内部优先阈值 2,048 字节和内部保留 65,536 字节。实际占用及长稳需真机测量。

平台音频数据、PCM FIFO/暂存区、允许的任务栈、提示音和 HTTPS wrapper 在生命周期允许时显式使用 PSRAM。LVGL 与两张 RGB framebuffer 使用外部内存；NVS 采用 IDF 的 PSRAM 优先 cache，TLS 使用外部分配。驱动自有 allocator 仍决定能力、对齐和 cache 要求。

同步对象、TCB、ISR 状态和会在关闭 cache 时执行 Flash/NVS 操作的栈按要求留在内部。保留池不保证单块连续空间。S31 支持部分 DMA 访问 PSRAM，不能照搬 S3 的所有 DMA 缓冲必须内部规则；遵循各 S31 外设驱动以及 RGB cache/VSYNC 同步要求。

PSRAM 栈使用 IDF capability-aware 任务 API。owner 等 I/O/回调结束和 worker 挂起后，才删除任务并释放栈；超时保留所有权供后续重试。PCM 测试覆盖失败、部分清理、超时和反复生命周期操作。

视频及 S3 DMA 缓冲 workaround 均不启用，不能按已删除的摄像头代码估算资源，也不直接复制 S3 cache/heap reserve。[验证记录](s31-validation.md)区分配置/构建检查与真机测量。


S31 的 AOSL 适配层对明确审计过的任务使用 PSRAM 栈：音频采集 `cap_mpq`、播放
`pb_mpq`、发送 `mybot_mpq`，HTTP/JSON 任务 `service_mpq`，以及项目 RTC 转发任务
`mybot_rtc`。双向会议的六个音频栈共 96 KiB，加上 HTTP 16 KiB、RTC 转发 8 KiB，
共减少约 120 KiB 内部栈需求；每个任务原有栈大小和优先级不变。

NVS 持久化由 `control_mpq` 执行，其栈继续内部；不透明的 RTSA `AgoraRtc`、
`AgoraRtcCb` 等任务也保留内部策略。外部栈使用 IDF WithCaps 创建/删除配对，退出时
依据实际栈地址判断，无须依赖可变化的任务名。任务创建日志显示栈位置，失败时记录
所请求的栈大小及内部/PSRAM 空闲量、最大连续块。

SDK PCM 队列、USB 媒体管线对象、字幕、RTC 事件拷贝、HTTPS/TLS 和 codec 暂存区已
使用 PSRAM。USB PIO 中断直接访问的输入/输出环形缓冲及发送包继续放内部 RAM；不能
仅因数据量大就迁走 ISR 所需内存。现有应用静态状态保留原布局，本次不启用会同时迁移
Wi-Fi/lwIP 静态区的全局外部 BSS 选项。
