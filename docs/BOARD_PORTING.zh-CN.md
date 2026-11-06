# S31 平台职责

[English](BOARD_PORTING.md) · [简体中文](BOARD_PORTING.zh-CN.md)

默认 profile 为 `esp-mosaico-v1.2`，旧 `esp32-s31-korvo-1` 保留，由根 CMake 的 `MYBOT_BOARD` 选择。本仓库是独立 S31 工程，旧 S3 板型和 generic 驱动已移除。

目录与 ESP32 工程保持一致：`components/mybot_stack/mybot_sdk/` 是组件包装及项目维护的听译 SDK，`components/mybot_stack/aosl/` 是 AOSL 上游源码及 S31 组件包装，`components/mybot_stack/agora_rtc/` 是固定 S31 RTSA；三者都是独立 ESP-IDF 组件。`main/` 负责启动、设备身份和 MyBot/网络生命周期，`components/mybot_platform/boards/esp-mosaico-v1.2/` 定义当前硬件和默认值，旧 `esp32-s31-korvo-1/` 保留 Korvo 硬件及 ADC 参考源。平台 `services/` 管理 Wi-Fi、HTTPS、NVS、提示音、PCM FIFO 和诊断，`drivers/` 管理 ES8311／ES8389、按键、触摸及 QSPI／RGB／LVGL，`internal/` 为私有接口。

平台仅使用 SDK 的公开 `include/mybot/` API。SDK 是项目维护的 V2VT 媒体／面对面／会议翻译分支，硬件细节留在平台组件；AOSL 除组件包装/S31 适配器外及保留的依赖快照仍不可修改。根 CMake 通过 `EXTRA_COMPONENT_DIRS` 分别注册三个运行时组件，`mybot_stack` 父目录只是 bundle。

板级 `prepare()` 在注册平台前准备硬件；`ensure_network()` 返回前必须获得可用网络。配网先停止 MyBot，连接恢复后返回。输入由板级长期持有，因此 SDK 回调脱离期间 AI（Mosaico）或 SET（Korvo）仍可请求配网。

Mosaico 使用 ES8311/I2S stereo/MCLK256；Korvo 使用 ES8389/TDM4。采集和播放独立使用引用计数的 codec 总线，对 SDK 保持 16 kHz、mono、signed-16 PCM，长度使用**帧数**。提示音结束需 drain。stop/destroy 有界等待；超时保留资源和所有权，后续初始化/清理可恢复。

板级 panel 文件拥有面板及 DMA 缓冲；Mosaico 使用独立绘制／暂存区的 QSPI 局部刷新，Korvo 使用 RGB framebuffer／VSYNC。LVGL adapter 协调刷新、触摸及共享 view 生命周期。语言选择直接调用非阻塞的 `mybot_ui_submit()`，按值复制命令并检查是否接收；音量、暂停和按键事件经 S31 输入工作线程提交。显示回调不执行网络操作，detach 后不再发出旧回调。显示板自动使用共享原生 LVGL 翻译界面，场景行为见[界面说明](PLATFORM_UI.zh-CN.md)。

Mosaico 的 CO5300 QSPI/CST9220 adapter 使用 480×480 方屏布局和内部 DMA 暂存，
RGB/GT1151 路径仅属于旧 Korvo。AI 键短按启停、长按配网，Type-C 提供复合 UAC2 音频及 CDC 日志。会议 LISTEN 输入电脑音频并播放到 codec，SPEAK 输入板载 MIC 并将译音送回 USB IN。

当前契约：ESP-IDF v6.1 preview `esp32s31`、16 MB Flash、Octal PSRAM、`partitions/v2/16m.csv`、板型对应的 codec、共享 LVGL、Newlib、60 ms RTSA。视频不可用。修改前参考[板卡说明](esp-mosaico-v1.2.zh-CN.md)、[内存策略](PSRAM_MEMORY.zh-CN.md)及[验证记录](s31-validation.md)。

生命周期/回调变化需 host 失败、超时、反复启停测试，并单独记录真机结果；编译不能证明引脚、音质或硬件就绪。
