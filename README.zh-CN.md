# MyBot ESP32-S31-Korvo-1 V1.1

[English](README.md) · [简体中文](README.zh-CN.md)

基于 **ESP-IDF v6.1**，唯一板型为 `esp32-s31-korvo-1`。包含 Wi-Fi 网页配网、HTTPS 配对、RTC/RTM、ES8389 TDM 双向语音、提示音、ADC 按键和 800×480 RGB/GT1151 LVGL 界面。视频暂不移植。

用户已在真机确认双向音频互通与屏幕正常显示。本次增加**轻点屏幕开始对话、再次轻点结束**，同时保留 PLAY 键；新的触摸交互待上板验收。各项结果的范围见[验证记录](docs/s31-validation.md)。

## 编译和烧录

准备 ESP-IDF v6.1 和 ffmpeg。固定版本 S31 RTSA 头文件/库已随工程提供，无需下载或手动导入：

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
idf.py --preview -B build/korvo-s31 -p PORT flash monitor
```

通过板载 USB-UART 接口烧录。英文构建使用独立目录/sdkconfig，并在 defaults 末尾追加 `;ci/en-us.defaults`。开发直接使用 `idf.py`。

当前 RTSA 固定使用 **60 ms** 音频帧。ffmpeg 在构建目录将原有 Ogg 提示音转换为 16 kHz mono S16LE；MyBot 接口保持 16 kHz、mono、signed 16-bit PCM，ES8389 物理总线采用四个 16-bit TDM 时隙。

## 文档和交付

- [板卡、操作与上板测试](docs/esp32-s31-korvo-1.zh-CN.md)
- [构建校验和合并固件交付](docs/RELEASING.zh-CN.md)
- [平台职责](docs/BOARD_PORTING.zh-CN.md)与[界面说明](docs/PLATFORM_UI.zh-CN.md)
- [贡献](CONTRIBUTING.zh-CN.md)、[支持](SUPPORT.zh-CN.md)、[依赖来源](VENDORED_SOURCES.md)和[许可声明](THIRD_PARTY_NOTICES.md)

自动 builder 可生成中英文交付文件：

```sh
python3 scripts/build_all.py --board esp32-s31-korvo-1 --language both
```

普通 builder 会清理 `build/`、`releases/`；增量构建使用 `--no-clean`。安全清理要求 Python 3.11+；IDF Python 3.10 可直接运行 `idf.py` 或增量 builder。

工程已裁为 S31，MyBot/AOSL/codec/LVGL 等源码快照完整保留，适配放在独立 wrapper 和平台层。固定 RTSA payload 随工程跟踪；原 tgz、配置、构建输出和固件不提交 Git。push/PR CI 自动检查源码并构建中英文，不自动发布 Release。
