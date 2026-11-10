# 参与贡献

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md)

仅支持 ESP-Mosaico CoreBoard V1.2，使用 ESP-IDF v6.1 preview 和 60 ms RTSA
音频帧。改动应聚焦目标，保留无关工作。

## 职责边界

应用启动和 MyBot 生命周期放在 `main/`，板级硬件、驱动和服务放在 `components/mybot_platform/`。平台仅通过 `components/mybot_stack/mybot_sdk/mybot/include/mybot/` 下的公开头文件使用 SDK。

MyBot SDK 是项目维护的 V2VT 媒体／面对面／会议翻译分支，保持 16 kHz mono S16 平台契约和 60 ms 节奏。Mosaico 默认使用 Local AEC；可选 Cloud AEC 保留交错 MIC/播放参考协议，不能与 Local AEC 叠加。采集 DSP 重置必须在 capture owner 内同步完成，再确认 PCM 屏障。AOSL、esp_codec_dev、LVGL、esp_lvgl_port 及固定 RTSA 头文件/库保持不变，AOSL 的组件 `CMakeLists.txt` 和 `s31_port/` 适配器除外。`mybot_sdk`、`aosl`、`agora_rtc` 三个运行时组件直接位于 `components/mybot_stack/`。AOSL 名为 `esp32-s3` 的可移植 HAL 仍由 S31 wrapper 使用，是不可删除的输入。

不提交凭据、设备标识、Wi-Fi 密码、原始 RTSA 归档、生成配置或固件。自维护 C/C++ 文件带 SPDX 头，使用与 CI 一致的精确版本 clang-format 18.1.8 按 `.clang-format` 格式化。通过 `python -m pip install clang-format==18.1.8` 安装，并用 `clang-format --version` 确认；发行版的 `clang-format-18` 软件包可能是不同补丁版本。生成 LVGL 源码时也使用该版本。行为、依赖或来源变化时同步文档、`CHANGELOG.md`、来源记录和许可证。

## 验证

安装 ffmpeg 和 ESP-IDF v6.1。固定版本 RTSA payload 随工程提供。开发直接使用 `idf.py` 构建中文，所有本地编译固定使用 `build/mosaico-v1.2` 和 `build/mosaico-v1.2/sdkconfig`：

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
idf.py --preview -B build/mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/mosaico-v1.2/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build size
python3 scripts/check_s31_build.py --build-dir build/mosaico-v1.2 --language zh-CN
git diff --check
```

CI 保留独立 job 目录，英文添加 `ci/en-us.defaults`；本地固定目录不改变 CI 矩阵。[CI](.github/workflows/ci.yml)定义源码检查、host 测试和固件变体。运行 `tests/` 下受影响的测试；生命周期/回调改动需覆盖初始化失败、部分清理、超时、反复启停和资源所有权。[交付文档](docs/RELEASING.zh-CN.md)说明在同一本地目录中合并镜像与执行交付校验。

在[验证记录](docs/s31-validation.md)分别记录源码检查、host 测试、目标构建与真机结果。编译通过不能证明触摸、声学质量或长稳表现。S31 命令需要 `--preview`，视频保持关闭。

Pull request 说明具体问题、变化后的行为、验证及剩余真机项。依赖版本、来源与许可需可审查。

## 提交信息

采用 [Conventional Commits](https://www.conventionalcommits.org/)，主题不超过 72 个字符：

```text
<type>[optional scope][!]: <subject>
```

类型为 `feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`build`、`ci`、`chore`、`revert`。正文与主题之间留空行；可运行 `./scripts/setup-githooks.sh` 安装本地提交信息校验。

贡献采用仓库许可证，文件另有适用条款时以文件为准。保留依赖的独立条款见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
