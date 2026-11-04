# ESP32-S31-Korvo-1 V1.1

[English](esp32-s31-korvo-1.en.md) · [简体中文](esp32-s31-korvo-1.zh-CN.md)

Board ID 为 `esp32-s31-korvo-1`，基于 ESP-IDF v6.1 的 preview S31 target。当前应用为板载 MIC → 云端 Bot → 板载喇叭的单向听译，包含 Wi-Fi/网页配网、HTTPS/配对、RTC/RTM、ES8389 并行采播、ADC 按键和 RGB/GT1151 界面。视频不在当前移植范围。

## 编译和烧录

安装 ffmpeg。固定版本 S31 RTSA 头文件/库随工程提供，直接编译：

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
idf.py --preview -B build/korvo-s31 -p PORT flash monitor
```

开发直接运行 `idf.py`。板载 USB-UART 用于烧录和 UART0 日志（TX58/RX59），独立供电 USB 口只供电。英文构建使用独立目录/sdkconfig，并在 defaults 末尾追加 `;ci/en-us.defaults`。修改 defaults 后应重新配置并检查 sdkconfig；旧配置不会自动被默认值覆盖。

提示音由 ffmpeg 在构建目录从 Ogg 转为 16 kHz mono S16LE，固件无需 S3 Opus 解码库。交付镜像与校验文件见[交付说明](RELEASING.zh-CN.md)。`idf.py flash` 日常烧录保留 NVS；合并镜像写入 **0x0**，覆盖 NVS 所在间隙，之后需重新配网。

## 操作

| 操作 | 行为 |
| --- | --- |
| 屏幕语言按钮 | 就绪时选择输入/目标语言并保存 |
| 屏幕开始/停止按钮 | 开始翻译、连接中取消、运行中停止、清理失败时重试 |
| 实时页暂停/继续 | 本地暂停与恢复音频，保留 RTC 父会话 |
| 音量滑条 | 松手提交一次绝对值，显示实际输出音量 |
| PLAY 短按 | 开始/停止翻译；连接中取消；清理失败时重试 |
| SET 短按 | 请求重新配对 |
| SET 长按 3 秒 | 停止 MyBot 后进入配网 |
| VOL+/VOL- | 调整音量并保存 |
| 首次未配置 Wi-Fi | 自动进入网页配网并显示入口/播放提示 |

等待 Wi-Fi 与配对完成后，从首页进入媒体翻译，选择输入语言与目标语言并开始。面对面、会议互译目前提供设置预览，字幕与亮度明确标为演示。只有实际控件触摸会触发业务，空白区域不会启停；显示回调只将命令按值复制入队，不执行阻塞的 HTTPS/RTC 操作。清理或未知结果会阻止新会话；设备端 MIC 与播放参考继续沿用云 AEC 配对上行。开发服务为 `https://mybot.sh3t.agoralab.co/api`，需绑定开启译音的 V2VT Bot。完整界面说明见[翻译界面](PLATFORM_UI.zh-CN.md)，配置与烧录见[独立听译](LISTEN_TRANSLATION.zh-CN.md)。

原有基础音频互通与显示有用户实机确认；当前同传路由、云 AEC、语言操作与恢复仍需本轮实机测试，不能沿用普通对话的验收结论。

## 硬件与 TDM

| 项目 | 当前配置 |
| --- | --- |
| 模组/存储 | ESP32-S31-WROOM-3，16 MB Flash、16 MB PSRAM |
| Codec | ES8389，I2C0 SDA0/SCL1 |
| 音频总线 | 16 kHz，4×16-bit TDM，64 BCLK/frame；BCLK3、WS4、TX5、RX6 |
| 时钟/功放 | Codec 使用 BCLK，CPU 不输出 MCLK；PA GPIO7 |
| SDK 音频 | mono signed 16-bit PCM、16 kHz、60 ms 帧 |
| LCD | 800×480 RGB565、26 MHz、两张 PSRAM framebuffer |
| 触摸 | GT1151，共用 I2C0；reset/IRQ 为 NC |
| 显示控制 | 背光和显示使能为 NC，不配置假定 GPIO |
| ADC 按键 | GPIO42 电阻梯，使用官方软件校准源 |
| 分区 | `partitions/v2/16m.csv`，两个 `0x3f0000` OTA slot |

按官方文档要求使用 TDM，未照搬固定 factory BSP 的标准 stereo I2S 初始化。ES8389 `use_mclk=false`、`no_dac_ref=false`，适配器检查 STDM 寄存器，播放映射时隙、采集选定麦克风时隙。名义 BCLK 为 1.024 MHz、WS 为 16 kHz，未声称经仪器测量。

基础音频互通通过不代表双麦/参考时隙归属、量化音质或长稳已验收；本地 AEC/波束成形/唤醒、视频、SD/USB 应用和低功耗均未移植。媒体翻译沿用板载 MIC 与扬声器路径，新界面与本地暂停需要各自的实机验收。

## 开启对话正文日志

所有 HTTPS 请求与响应正文均打印，包括配对、binding-status、翻译 start/stop 和 HTTP 错误响应。筛选 `event=http_body`，用连接 `id` 和 `direction=request|response` 归组。每个正文一次完整输出到单条 `phase=data` 日志，空正文输出 `data=`；`begin/end` 保留长度和完整性信息，未完成的正文标记 `complete=no`。begin 还提供方法、脱敏路径、状态、实际与捕获字节数及传输错误。正文捕获有界（请求含头 2 KiB，响应含头 32 KiB），超限会明确标记。含 NUL 的正文一次输出完整 hex，临时 PSRAM 分配失败会标记输出不可用。日志可能含认证和设备信息，分享前脱敏。时间同步、启动超时和未知会话恢复见[听译排查](LISTEN_TRANSLATION.zh-CN.md#启动排查与恢复)。

## 参考

硬件事实和 ADC 校准源来自 [Espressif 官方文档](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html)及 [esp-dev-kits 固定 BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo)。只引入所需硬件参数与授权 ADC 源，不依赖 factory_demo 的应用或素材。具体来源和条款见根目录来源/许可文件。
