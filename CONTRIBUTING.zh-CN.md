# 参与贡献

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md)

本仓库仅支持 ESP32-S31-Korvo-1 V1.1，使用 ESP-IDF v6.1 和 60 ms RTSA 音频帧。改动应聚焦目标，保留无关工作。

## 职责边界

应用启动和 MyBot 生命周期放在 `main/`，板级硬件、驱动和服务放在 `components/mybot_platform/`。平台仅通过 `components/mybot_stack/mybot_sdk/mybot/include/mybot/` 下的公开头文件使用 SDK。

MyBot、AOSL、esp_codec_dev、LVGL、esp_lvgl_port 源码快照保持不变。S31 AOSL/RTSA wrapper 位于 `components/mybot_platform/ports/`；本地组件 CMake wrapper 可按需注册源码，不修改快照。AOSL 名为 `esp32-s3` 的可移植 HAL 仍由 S31 wrapper 使用，是不可删除的输入。

不提交凭据、设备标识、Wi-Fi 密码、原始 RTSA 归档、生成配置或固件。自维护 C/C++ 文件带 SPDX 头，改动文件按 `.clang-format` 格式化。行为、依赖或来源变化时同步文档、`CHANGELOG.md`、来源记录和许可证。

## 验证

安装 ffmpeg 和 ESP-IDF v6.1。固定版本 RTSA payload 随工程提供，开发直接使用 `idf.py`：

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --version)" = "ESP-IDF v6.1"
idf.py --preview -B build/contribution \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/contribution/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build size
python3 scripts/check_s31_build.py --build-dir build/contribution --language zh-CN
git diff --check
```

英文使用独立目录，并添加 `ci/en-us.defaults`。[CI](.github/workflows/ci.yml)定义源码检查、host 测试和固件变体。运行 `tests/` 下受影响的测试；生命周期/回调改动需覆盖初始化失败、部分清理、超时、反复启停和资源所有权。[交付文档](docs/RELEASING.zh-CN.md)说明双语打包。

在[验证记录](docs/s31-validation.md)分别记录源码检查、host 测试、目标构建与真机结果。编译通过不能证明触摸、声学质量或长稳表现。S31 命令需要 `--preview`，视频保持关闭。

Pull request 说明具体问题、变化后的行为、验证及剩余真机项。依赖版本、来源与许可需可审查。

## 提交信息

采用 [Conventional Commits](https://www.conventionalcommits.org/)，主题不超过 72 个字符：

```text
<type>[optional scope][!]: <subject>
```

类型为 `feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`build`、`ci`、`chore`、`revert`。正文与主题之间留空行；可运行 `./scripts/setup-githooks.sh` 安装本地提交信息校验。

贡献采用仓库许可证，文件另有适用条款时以文件为准。保留依赖的独立条款见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
