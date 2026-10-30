# 构建校验与交付

[English](RELEASING.md) · [简体中文](RELEASING.zh-CN.md)

工程仅支持 `esp32-s31-korvo-1`。RTSA v1.10.1 build 1366494 的固定头文件/库随工程提供，位于 `components/mybot_platform/ports/agora_rtc/vendor`；编译只需 ESP-IDF v6.1 和 ffmpeg，无需下载或导入。输入检查验证固定 payload hash。音频固定 60 ms，视频关闭。

## 本地构建

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --version)" = "ESP-IDF v6.1"
python3 scripts/build_all.py --board esp32-s31-korvo-1 --language both
```

普通 builder 会清理 `build/` 和 `releases/`，增量构建用 `--no-clean`。安全清理要求 Python 3.11+；开发可继续用[直接 idf.py 命令](esp32-s31-korvo-1.zh-CN.md)。

builder 对每种语言执行 reconfigure/build/size/merge-bin，再校验构建元数据和合并镜像。单独检查已有直接构建，或为其合并镜像生成交付信息：

```sh
python3 scripts/check_s31_build.py --build-dir build/korvo-s31 --language zh-CN
python3 scripts/check_s31_build.py --build-dir build/korvo-s31 --language zh-CN \
  --merged releases/esp32-s31-korvo-1-zh-CN.bin
```

`--merged` 校验已有合并镜像并写配套文件，本身不编译或合并固件。检查项包括目标/配置、RTSA 固定输入、最终链接对象来源、分区/镜像边界及镜像内实际写入段。

## 交付文件

每种语言对应四个文件，例如：

```text
esp32-s31-korvo-1-zh-CN.bin
esp32-s31-korvo-1-zh-CN.manifest.json
esp32-s31-korvo-1-zh-CN.sha256
esp32-s31-korvo-1-zh-CN.flash.txt
```

manifest 记录项目/IDF 版本、源码版本及 dirty 状态、语言、板型/target、RTSA 包/hash、合并镜像 hash 和实际段偏移/hash。它表示构建来源，不代表真机验收。交付时保留四个文件。

合并镜像烧录地址为 **0x0**，padding 覆盖 NVS/OTA 状态，需要重新配网。日常用 `idf.py --preview -B <build-dir> -p PORT flash monitor` 保留 NVS。

## CI

push/PR 自动执行源码检查、host 测试和中英文固件构建，直接使用随工程的固定 RTSA。每种语言单独上传上述四项 artifact；不上传原 tgz、sdkconfig、link map 或 NVS，不自动创建 GitHub Release。

本地验证与远端执行分开记录。远端 CI 需实际运行后才可标为通过，当前状态见[验证记录](s31-validation.md)。

正式交付前更新版本/changelog、核对依赖声明及新生成校验和；记录命令与结果，区分 host/构建/真机结果，并附现有限制。发布、tag、提交仅在用户明确指示后执行，准备本地交付文件不等于发布。
