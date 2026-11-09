# mybot-esp32s31

[![CI](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml/badge.svg)](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/junlon2006/mybot-esp32s31)](LICENSE)

**[English](README.md) | [简体中文](README.zh-CN.md)**

`mybot-esp32s31` 是基于 ESP-IDF 的独立 V2VT 翻译固件，运行于
**ESP-Mosaico CoreBoard V1.2**，后续设计以此为目标板。媒体翻译将板载麦克风音频送往云端，并通过板载喇叭播放译音，
默认中文译英文。面对面场景支持中文与英文轮流交流，点击语言按钮切换麦克风方向。
Mosaico 会议互译将电脑原声的译音送往设备喇叭，并通过 USB 将板载麦克风的译音送回电脑。
工程维护 MyBot 翻译 SDK 分支及板级、网络、音频、显示和生命周期集成。
开发 API 为 `https://mybot.sh3t.agoralab.co/api`。

当前开发基线为 **ESP-IDF v6.1**。仅支持 `esp-mosaico-v1.2`，Flash、Octal PSRAM、分区表和硬件驱动
由工程固定。具体硬件配置、输入操作、构建
命令和限制见[板卡说明](docs/esp-mosaico-v1.2.zh-CN.md)。

> 除非文件另有声明，本工程自维护代码使用 Apache-2.0。仓库内依赖和媒体资源有各自的
> 许可条款；重新分发源码或固件前请阅读[许可证与依赖](#许可证与依赖)。

## 当前能力

- Wi-Fi STA 自动重连与首次启动网页配网。
- 使用 NVS 持久化设备凭据、语言选择、会话恢复记录和扬声器音量。
- 通过 `esp-tls`、系统 CA bundle、SNI 与 hostname 校验实现 HTTPS。
- 16 kHz 单声道 signed-16 PCM 采集与播放；随附 RTSA 固定使用 60 ms 音频包，其他包长需要
  匹配的 RTSA 构建。
- Agora RTSA 麦克风/参考音频上行、Cloud AEC、可选 AI QoS（默认关闭）、RTM 频道订阅，并订阅响应指定的译音发布者。
- 共享有界 PCM 播放缓冲；采集/播放独立挂载，共享引用计数的 codec/I2S 资源。
- Mosaico 使用 ES8311 标准 I2S stereo 和 MCLK256；板级完成 SDK mono PCM 转换。
- 480×480 CO5300 QSPI 屏和 CST9220 触摸使用方屏 LVGL 布局。
- 原生媒体与面对面界面支持语言选择、启停、本地暂停和实际音量；翻译启停只使用触摸屏。
  面对面共用一个父会话、两个 RTC 连接和一个 RTM 频道订阅；Mosaico 会议场景支持电脑英语译中文到设备喇叭，同时将设备麦克风中文译英文回传电脑。
- 提供中英文配对码与 Wi-Fi 配网提示，并在配网页面显示实际热点名称。

## 硬件

固定目标为 `esp-mosaico-v1.2`：16 MB NOR Flash、16 MB Octal PSRAM、ES8311、
480×480 CO5300 屏和 CST9220 触摸。沿用 `partitions/v2/16m.csv`；128 MB NAND 暂不使用。
视频不启用。

用户确认了 PSRAM 栈修正版（`e279926`）的
基础 USB 双向会议互译，以及 40 行刷新缓冲的正常字幕显示；触摸导航及启动请求有实机日志。
独立 UAC 麦克风／喇叭测试另有通过记录。
此后的 review 修复已有 host 测试和中文构建结果，尚未重新进行真机验收。
音质、Cloud AEC、重连／长稳、USB 电流及修订后的 VCC 软启动仍需单独验证。操作见[板卡说明](docs/esp-mosaico-v1.2.zh-CN.md)，
范围见[验证记录](docs/s31-validation.md)。

## 构建

按照[官方安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.1/esp32s31/get-started/index.html)
安装 ESP-IDF v6.1 和 `ffmpeg`，并在当前 shell 中激活 ESP-IDF：

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
```

使用与 CI 相同的入口生成中英文交付固件：

```sh
python3 scripts/build_all.py --build-root build/delivery --output-root releases/delivery
```

CI 按语言调用同一脚本，并使用临时独立构建／交付根目录。脚本统一执行 reconfigure、build、
size、merge-bin、构建来源和交付校验。单板日常增量开发继续使用 `build/korvo-s31`：

```sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

[交付文档](docs/RELEASING.zh-CN.md)说明脚本的独立根目录、构建来源和交付文件。
直接烧录命令和板级硬件信息见
[板卡说明](docs/esp-mosaico-v1.2.zh-CN.md)。

固定版本 RTSA 头文件和库随工程提供于 `components/mybot_stack/agora_rtc`，无需下载或
手动导入。`ffmpeg` 在构建目录将已有 Ogg 提示音转换为 16 kHz mono S16LE。

## 配网与控制

NVS 中没有 Wi-Fi 凭据时，设备创建名为 `mybot-xxxx` 的配置 AP，并在
`http://192.168.4.1` 提供配网页面。STA 获取可用 IP 后才启动 MyBot。请求配网时，固件先
停止 MyBot，联网后重新启动。

进入媒体翻译，选择输入/目标语言后使用开始/停止控件；暂停保留云会话，空白区域轻点不执行
操作。Mosaico 的 AI 键是设备维护键：短按小于 3 秒无动作，按住 3 秒停止 MyBot 并进入
Wi-Fi 配网，继续按住到 10 秒会擦除本地 NVS 后重启并进入配网。屏幕内可调整音量并保存到
NVS。字幕、可滚动全文和本次历史接收实时 RTM 原文/译文；详见
[翻译界面](docs/PLATFORM_UI.zh-CN.md)。

Mosaico 翻译固件通过 Type-C 提供 UAC2 声卡与 CDC 日志串口。会议互译同时处理电脑
音频与板载麦克风，两路译音分别去设备喇叭和电脑 USB 麦克风；USB 与 SDK 边界完成
48 kHz / 16 kHz 转换。操作见 [USB 接入说明](docs/MOSAICO_USB_AUDIO.zh-CN.md)。
已通过的 [UAC 最小测试](diagnostics/mosaico_uac/README.zh-CN.md)作为独立 USB 音频参考保留。

使用 `idf.py --preview -B build/korvo-s31 menuconfig` 的 `mybot` 菜单选择产品语言、
初始翻译语言、Cloud AEC 和 AI QoS。产品语言决定界面文字和提示音；两个构建均使用
`https://mybot.sh3t.agoralab.co/api`。运行时保存的语言选择优先于初始翻译语言。

随附 RTSA 仅接受 60 ms 音频帧。可设置 `CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR=y` 启用 CPU、
堆和播放诊断，默认关闭。播放行为和诊断说明见[音频播放缓冲](docs/AUDIO_PLAYBACK.zh-CN.md)。
应用数据和工作任务栈遵循[PSRAM 内存策略](docs/PSRAM_MEMORY.zh-CN.md)；硬件 DMA、内核
对象和会写 Flash 的主任务栈保留内部 RAM。

## 验证

构建覆盖、已记录的真机结果和未支持能力统一维护在[验证记录](docs/s31-validation.md)。
本地开发构建中文固件；CI 覆盖中英文并验证链接和交付元数据。记录分别列出本地构建、
远端 CI 和真机结果。CI 构建和宿主测试不能证明音质、屏幕行为、上电时序、
Wi-Fi 恢复或长稳表现。

## 文档

- [板卡说明](docs/esp-mosaico-v1.2.zh-CN.md)
- [独立听译与烧录](docs/LISTEN_TRANSLATION.zh-CN.md)
- [验证记录](docs/s31-validation.md)
- [验证索引](docs/VALIDATION.md)
- [板级移植](docs/BOARD_PORTING.zh-CN.md)
- [PSRAM 内存策略](docs/PSRAM_MEMORY.zh-CN.md)
- [音频播放缓冲](docs/AUDIO_PLAYBACK.zh-CN.md)
- [平台 LVGL UI](docs/PLATFORM_UI.zh-CN.md)
- [构建校验与交付](docs/RELEASING.zh-CN.md)
- [参与贡献](CONTRIBUTING.zh-CN.md)
- [支持](SUPPORT.zh-CN.md)

## 许可证与依赖

除非文件带有其他 SPDX 标识，本工程自维护代码使用 [Apache-2.0](LICENSE)。仓库还包含
采用独立许可证或分发条款的依赖和媒体资源，包括 AOSL 与 Agora RTSA。使用或重新分发前
请阅读 [VENDORED_SOURCES.md](VENDORED_SOURCES.md) 与
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
