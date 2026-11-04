# mybot-esp32s31

[![CI](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml/badge.svg)](https://github.com/junlon2006/mybot-esp32s31/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/junlon2006/mybot-esp32s31)](LICENSE)

**[English](README.md) | [简体中文](README.zh-CN.md)**

`mybot-esp32s31` 是基于 ESP-IDF 的独立 `v2vt/listen` 听译固件，运行于
ESP32-S31-Korvo-1 V1.1。媒体翻译将板载麦克风音频送往云端，并通过板载喇叭播放译音，
默认中文译英文。工程维护 MyBot 听译 SDK 分支及板级、网络、音频、显示和生命周期集成。
开发 API 为 `https://mybot.sh3t.agoralab.co/api`。

当前开发基线为 **ESP-IDF v6.1**。唯一支持的 Board profile 为 `esp32-s31-korvo-1`，通过
`MYBOT_BOARD` 选择 Flash、Octal PSRAM、分区表和硬件驱动。具体硬件配置、输入操作、构建
命令和限制见[板卡说明](docs/esp32-s31-korvo-1.zh-CN.md)。

> 除非文件另有声明，本工程自维护代码使用 Apache-2.0。仓库内依赖和媒体资源有各自的
> 许可条款；重新分发源码或固件前请阅读[许可证与依赖](#许可证与依赖)。

## 当前能力

- Wi-Fi STA 自动重连与首次启动网页配网。
- 使用 NVS 持久化设备凭据、语言选择、会话恢复记录和扬声器音量。
- 通过 `esp-tls`、系统 CA bundle、SNI 与 hostname 校验实现 HTTPS。
- 16 kHz 单声道 signed-16 PCM 采集与播放；随附 RTSA 固定使用 60 ms 音频包，其他包长需要
  匹配的 RTSA 构建。
- Agora RTSA 麦克风/参考音频上行、Cloud AEC、AI QoS、RTM 频道订阅，并订阅响应指定的译音发布者。
- 单一支持的板型使用共享有界 PCM 播放缓冲和独立 TDM writer。
- ES8389 使用四个 16-bit Philips TDM 时隙；采集/播放独立挂载，并共享引用计数的 codec/I2S
  资源。
- 800 x 480 RGB565 显示板自动启用 LVGL，支持 GT1151 触摸。
- 原生媒体翻译界面支持语言选择、启停、本地暂停和实际音量；PLAY 同样支持启停。
  面对面和会议场景提供设置预览。
- 提供中英文配对码与 Wi-Fi 配网提示，并在配网页面显示实际热点名称。

## 支持的板卡

唯一支持的 profile 为 `esp32-s31-korvo-1`。

目标模组为 ESP32-S31-WROOM-3，配置 16 MB Flash、16 MB Octal PSRAM、ES8389 codec、
800 x 480 RGB565 面板、GT1151 触摸和 GPIO42 ADC 按键。固件使用
`partitions/v2/16m.csv`，当前不提供视频能力。

用户已在真机确认继承的基础双向音频和屏幕正常显示；翻译路由订阅、语言操作、Cloud AEC
和恢复仍需各自的真机结果。操作方式见[板卡说明](docs/esp32-s31-korvo-1.zh-CN.md)，各结果的验证范围见
[验证记录](docs/s31-validation.md)。

## 构建

按照[官方安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.1/esp32s31/get-started/index.html)
安装 ESP-IDF v6.1 和 `ffmpeg`，并在当前 shell 中激活 ESP-IDF：

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
```

使用独立构建目录和 sdkconfig 构建板型：

```sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

使用工程 builder 构建中英文固件：

```sh
python3 scripts/build_all.py --board esp32-s31-korvo-1 --language both
```

脚本将中间构建放在 `build/`，将合并镜像放在 `releases/`；固定使用随附 RTSA 的 60 ms
音频包，并校验 target 配置、RISC-V 链接来源、RTSA 成员、本地 AOSL 所有权、flash segment
和交付元数据。可使用 `--dry-run` 检查命令，`--no-clean` 仅用于增量构建。

构建与输出根目录必须专用、互不嵌套，路径不得包含 `..` 或符号链接分量。常规运行会清理
由脚本管理的目录；已有非空目录若没有匹配的 `.mybot-generated-root.json` 标记会被拒绝。
安全清理要求 Python 3.11 或更高版本。直接烧录命令和板级硬件信息见
[板卡说明](docs/esp32-s31-korvo-1.zh-CN.md)。

固定版本 RTSA 头文件和库随工程提供于 `components/mybot_stack/agora_rtc`，无需下载或
手动导入。`ffmpeg` 在构建目录将已有 Ogg 提示音转换为 16 kHz mono S16LE。

## 配网与控制

NVS 中没有 Wi-Fi 凭据时，设备创建名为 `mybot-xxxx` 的配置 AP，并在
`http://192.168.4.1` 提供配网页面。STA 获取可用 IP 后才启动 MyBot。请求配网时，固件先
停止 MyBot，联网后重新启动。

进入媒体翻译，选择输入/目标语言后使用开始/停止控件；暂停保留云会话，空白区域轻点不执行
操作。PLAY 短按开始/停止翻译、取消待启动会话或重试清理。SET 短按请求配对，SET 长按
3 秒请求配网，VOL+/VOL- 调整并持久化音量到 NVS。字幕、全文、历史和亮度均标为演示，
详见[翻译界面](docs/PLATFORM_UI.zh-CN.md)。

使用 `idf.py --preview -B <build-dir> menuconfig` 的 `mybot` 菜单选择产品语言、
初始翻译语言、Cloud AEC 和 AI QoS。产品语言决定界面文字和提示音；两个构建均使用
`https://mybot.sh3t.agoralab.co/api`。运行时保存的语言选择优先于初始翻译语言。

随附 RTSA 仅接受 60 ms 音频帧。可设置 `CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR=y` 启用 CPU、
堆和播放诊断，默认关闭。播放行为和诊断说明见[音频播放缓冲](docs/AUDIO_PLAYBACK.zh-CN.md)。
应用数据和工作任务栈遵循[PSRAM 内存策略](docs/PSRAM_MEMORY.zh-CN.md)；硬件 DMA、内核
对象和会写 Flash 的主任务栈保留内部 RAM。

## 验证

构建覆盖、已记录的真机结果和未支持能力统一维护在[验证记录](docs/s31-validation.md)。
本地 CI 等价检查构建并合并中英文固件，验证链接和交付元数据；记录分别列出本地构建、
远端 CI 和真机结果。CI 构建和宿主测试不能证明音质、屏幕行为、上电时序、
Wi-Fi 恢复或长稳表现。

## 文档

- [板卡说明](docs/esp32-s31-korvo-1.zh-CN.md)
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
