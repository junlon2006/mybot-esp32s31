# 构建校验与交付

[English](RELEASING.md) · [简体中文](RELEASING.zh-CN.md)

唯一交付板为 `esp-mosaico-v1.2`。RTSA v1.10.1 build 1366494 的固定头文件/库随工程提供，位于 `components/mybot_stack/agora_rtc`；编译只需 ESP-IDF v6.1 和 ffmpeg，无需下载或导入。输入检查验证固定 payload hash。音频固定 60 ms，视频关闭。

## 本地构建

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
python3 scripts/build_all.py --language both \
  --build-root build/delivery --output-root releases/delivery
```

该入口与 CI 相同：为每种语言创建独立 build/sdkconfig 目录，再统一执行配置、编译、
size、merge-bin 和交付校验。CI 使用临时独立构建／交付根目录；本地交付使用上面的专用根目录。
单板日常增量开发继续使用 `idf.py --preview -B build/mosaico-v1.2`。

检查项包括目标／配置、RTSA 固定输入、最终链接与实际写入段，并读取已构建分区表，
要求所有 OTA 应用槽至少留出 **300 KiB**。生成目录只有在所有权标记校验通过后才会清理，
非空且无标记的目录会被拒绝。

构建在编译前记录 Git 版本、dirty 状态、工程输入与配置摘要，将身份嵌入固件，再绑定
完成后的 ELF/BIN 和烧录段。打包读取该冻结记录，更换 checkout 不会替旧固件改写来源。
记录缺失或 hash 不一致时必须重新构建；单独 reconfigure 或修改交付元数据不能证明来源。

## 交付文件

每种语言在所选输出目录中对应四个文件，例如：

```text
esp-mosaico-v1.2-zh-CN.bin
esp-mosaico-v1.2-zh-CN.manifest.json
esp-mosaico-v1.2-zh-CN.sha256
esp-mosaico-v1.2-zh-CN.flash.txt
```

manifest 记录项目／IDF 版本、绑定构建的源码版本及 dirty 状态、语言、板型／target、RTSA 包／hash、构建与合并镜像 hash、段偏移和 OTA 余量。它表示构建来源，不代表真机验收。交付时保留四个文件。

合并镜像烧录地址为 **0x0**，padding 覆盖 NVS／OTA 状态，需要重新配网。日常用 `idf.py --preview -B build/mosaico-v1.2 -p PORT flash` 保留 NVS。运行时 CDC 与 ROM 下载端口不同；随后选择当前端口执行 `idf.py --preview -B build/mosaico-v1.2 -p RUNTIME_PORT monitor`。

## CI

push/PR 执行源码检查、host 测试，并通过 `scripts/build_all.py` 构建中英文固件矩阵，
直接使用随工程的固定 RTSA。各 job 使用临时独立构建／配置并上传上述四项 artifact；
不上传原 tgz、sdkconfig、link map 或 NVS，不自动创建 GitHub Release。

本地验证与远端执行分开记录。远端 CI 需实际运行后才可标为通过，当前状态见[验证记录](s31-validation.md)。

正式交付前更新版本/changelog、核对依赖声明及新生成校验和；记录命令与结果，区分 host/构建/真机结果，并附现有限制。发布、tag、提交仅在用户明确指示后执行，准备本地交付文件不等于发布。
