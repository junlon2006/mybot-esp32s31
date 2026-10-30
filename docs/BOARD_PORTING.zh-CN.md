# S31 平台职责

[English](BOARD_PORTING.md) · [简体中文](BOARD_PORTING.zh-CN.md)

唯一支持 profile 为 `esp32-s31-korvo-1`，由根 CMake 的 `MYBOT_BOARD` 选择。本仓库是独立 S31 工程，旧 S3 板型和 generic 驱动已移除。

目录与 ESP32 工程保持一致：`components/mybot_stack/mybot_sdk/` 是 SDK wrapper/快照，`components/mybot_stack/aosl/` 是 AOSL 上游源码及 S31 组件包装，`components/mybot_stack/agora_rtc/` 是固定 S31 RTSA；三者都是独立 ESP-IDF 组件。`main/` 负责启动、设备身份和 MyBot/网络生命周期，`components/mybot_platform/boards/esp32-s31-korvo-1/` 定义硬件、默认值、分区和 ADC 参考源。平台 `services/` 管理 Wi-Fi、HTTPS、NVS、提示音、PCM FIFO 和诊断，`drivers/` 管理 ES8389、ADC 键、触摸和 RGB/LVGL，`internal/` 为私有接口。

平台仅使用 SDK 的公开 `include/mybot/` API，硬件细节不进入不可变源码快照。根 CMake 通过 `EXTRA_COMPONENT_DIRS` 分别注册三个运行时组件，`mybot_stack` 父目录只是 bundle。

板级 `prepare()` 在注册平台前准备硬件；`ensure_network()` 返回前必须获得可用网络。配网先停止 MyBot，连接恢复后返回。输入由板级长期持有，因此 SDK 回调脱离期间 SET 仍可请求配网。

采集和播放独立使用引用计数的 ES8389/TDM 总线，对 SDK 保持 16 kHz、mono、signed-16 PCM，长度使用**帧数**。提示音结束需 drain。stop/destroy 有界等待；超时保留资源和所有权，后续初始化/清理可恢复。

RGB panel 文件拥有面板/缓冲，S31 LVGL adapter 协调 VSYNC、刷新、触摸和共享 view 生命周期。触摸经 S31 输入 owner 转为公开按键事件，显示回调不执行网络操作，detach 后不再发出旧回调。所有显示使用共享 LVGL，主题和动画是构建选项。

当前契约：ESP-IDF v6.1 preview `esp32s31`、16 MB Flash、Octal PSRAM、`partitions/v2/16m.csv`、ES8389、共享 LVGL、Newlib、60 ms RTSA。视频不可用。修改前参考[板卡说明](esp32-s31-korvo-1.zh-CN.md)、[内存策略](PSRAM_MEMORY.zh-CN.md)及[验证记录](s31-validation.md)。

生命周期/回调变化需 host 失败、超时、反复启停测试，并单独记录真机结果；编译不能证明引脚、音质或硬件就绪。
