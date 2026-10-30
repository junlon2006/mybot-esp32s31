# 历史移植方案（2026-10-30）

以下为实施前方案，包含当时的待定项与后续阶段，不代表当前支持范围。当前仅维护 S31 无视频版本；以 [S31 板卡文档](../esp32-s31-korvo-1.zh-CN.md)、[验证记录](../s31-validation.md)和根目录 README 为准。

> 实施状态（2026-10-30）：无视频首版已实现；当前构建与测试结果以 README.md 和 docs/s31-validation.md 为准。以下保留实施前方案。

# MyBot 移植 ESP32-S31-Korvo-1 V1.1 方案

日期：2026-10-30。目标项目：`/Users/shangjinlong/Desktop/workspace/github/mybot-esp32s31`。

## 1. 结论与范围

可以以当前 MyBot 的 ESP-IDF 6.1 工程为基础移植。新 RTSA 包的公开头文件与现有版本一致，MyBot 的配网、HTTPS、配对、会话、音频/视频公开接口及共享 UI 可以复用。主要工作集中在 RISC-V 依赖封装，以及 ES8389 音频总线、RGB LCD/GT1151、DVP 摄像头和硬件 JPEG 的平台适配。

建议新项目只维护这一款板，board ID 使用 `esp32-s31-korvo-1`，目标 SoC 为 `esp32s31`。首个交付目标是稳定的双向语音，之后接入共享 LVGL 界面，最后增加摄像头上传。第一轮暂缓本地唤醒、双麦波束成形/AEC、SD 卡、USB Host 和 PPA 加速。

本文件是迁移方案和只读审计记录；目前尚未在目标项目中移植代码、链接新 RTSA 或完成真机验证。

## 2. 固定基线与参考

| 项目 | 基线 |
| --- | --- |
| MyBot 源工程 | `/Users/shangjinlong/Desktop/workspace/github/mybot-esp32`，commit `2782cc88f52c8a46419654b72a918a8273cc23bf` |
| 新项目初始状态 | commit `b11455a68363d1e64342067a18ceca5feac7be98`；已有 `.git`、README、LICENSE、`.gitignore` 和用户提供的 tgz |
| ESP-IDF | `/Users/shangjinlong/esp/esp-idf`，`v6.1`；S31 是预览目标 |
| MyBot SDK 快照 | `baf41bac3b6ca25bad9820f9fc857c94698607d8`，保持源码不变 |
| AOSL 快照 | `84e086084ebcd0ae2455a0ce5721950c5fe2e656`，保持源码不变，增加外部 S31 封装 |
| 硬件 | ESP32-S31-Korvo-1 V1.1，配 ESP32-S3-LCD-EV-Board-SUB3 子板 |
| 官方示例 | 用户指定的 esp-dev-kits commit `bd4d4ac18657e73583af3d7c0b0a23c3cddf3735` 中的 factory_demo 与配套 BSP |

硬件与 demo 参考：

- [ESP32-S31-Korvo-1 V1.1 用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#esp32-s31-korvo-1-v1-1)
- [固定版本 factory_demo](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac/examples/esp32-s31-korvo/examples/factory_demo)
- [同版本 BSP 所在目录](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo)
- [BSP 实现](https://github.com/espressif/esp-dev-kits/blob/bd4d4ac/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/esp32_s31_korvo.c)
- [显示参数](https://github.com/espressif/esp-dev-kits/blob/bd4d4ac/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/include/bsp/display.h#L30)
- [音频默认参数](https://github.com/espressif/esp-dev-kits/blob/bd4d4ac/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/include/bsp/audio.h#L27)
- [BSP 依赖 manifest](https://github.com/espressif/esp-dev-kits/blob/bd4d4ac/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/idf_component.yml)

本次已读取官方用户指南，并通过 GitHub blob 页面核对上述固定 commit 的 BSP 实现、板级头文件、display.h、audio.h 和 manifest。先前的 master 示例说明/defaults 缓存仅作为补充，不能替代固定版本源码。实施时应固定依赖解析结果和原理图版本，并先在参考 demo 上完成外设基准验证。

该 BSP manifest 指定 `esp_codec_dev ^1.5.9`、`esp_video 2.4.1`、`esp_cam_sensor 2.4.*`、GT1151 `^1`、`esp_lvgl_adapter ^0.7.0`，以及 button/led_strip。移植时选择所需组件并锁定实际验证版本。MyBot 继续使用自己的共享 LVGL 视图和适配层，不直接引入整个 demo 或同时运行两套 LVGL 管理器。

## 3. RTSA 包审计与链接门槛

用户提供的原始输入：

```text
Agora-RTSALite-SRmRcAcAj-riscv32-rtos-elf-16-esp32s31-v1.10.1-20261030_150429-1366494.tgz
SHA-256: 7c6a81ab91f3b44c0cd581f84f779d7a022dac159020c19bdddd07c95da31dea
```

| 检查项 | 已确认结果 | 后续动作 |
| --- | --- | --- |
| 公开 API | SDK v1.10.1，build 1366494；`agora_rtc_api.h` 与现有版本逐字一致 | 复用 MyBot RTC 接口层，使用新包的头文件和库成对导入 |
| 音频/RTM | G.722、音频 jitter buffer、RTM/channel/string UID 已启用；最小 timer interval 为 60 ms | 初期沿用现有 16 kHz mono S16 PCM 与 60 ms 应用配置，验证实际上下行帧长；timer interval 不等于任意音频 ptime 的兼容保证 |
| JPEG 传输 | 有 `VIDEO_DATA_TYPE_GENERIC_JPEG=20`、`agora_rtc_send_video_data` 和 JPEG 实现成员 | 复用公开上传接口；采集和 JPEG 编码由平台实现 |
| RISC-V ABI | 126 个成员为 ELF32 little-endian RISC-V，RVC、single-float ABI，16 字节栈对齐，整体对应 ilp32f | 检查最终目标 ABI、链接 map、运行结果 |
| 编译器 | 包使用 GCC 16.1.0 / esp-16.1.0_20260609；本机 IDF 6.1 配套 GCC 15.2.0 / esp-15.2.0_20251204 | 核对供应方编译参数；版本不同本身不代表不兼容，不先替换 IDF 工具链 |
| ISA | 包有 Zb/Zc 等属性及实际位操作指令，`xespv2p1`；当前 S31 hello ELF 为 `xespv2p2` | S31 数据手册支持 Zb/Zc，不能因 IDF 默认 march 未列出就判断不支持；仍需核对完整 march、芯片版本并上板运行 |
| 混合归档 | 同一 `.a` 还含 49 个 x86-64 AOSL/HAL 成员；部分 AOSL 符号索引指向这些成员 | 必须由本地 S31 AOSL 满足依赖，检查最终 map 不抽取 x86 对象；不要对 RTSA 使用 whole-archive |
| libc | 新 RISC-V cJSON 引用 `tolower`，没有发现旧 S3 包的 `_ctype_` 依赖 | 不能直接套用“S31 必须 Newlib”的结论；先以 Newlib 为保守候选，再按新库 ABI、完整链接和运行结果冻结选择 |

现有能工作的 S3 RTSA 包也包含同类 x86 AOSL 成员，因此“混合归档”不能单独证明新包不可用。优先取得供应方提供的纯 RISC-V 包或 RTSA core-only 包；若继续使用当前包，保留原包及哈希，通过显式链接关系确保本地 AOSL 正确解析。普通前后顺序或 link-group 本身不能保证安全，因为扫描 RTSA 时可能先从同一归档抽取 x86 AOSL 成员。P0 应验证本地 S31 AOSL 实现预先进入链接的方案，例如仅对本地 `aosl` 组件启用 IDF 的 `WHOLE_ARCHIVE`，并用最终 map 确认符号来源；RTSA 本身仍禁止 whole-archive。如无法得到可证明正确的链接结果，应要求供应方提供纯 RISC-V/core-only 包。不要静默删除 archive 成员。

包内 `bin/riscv32/aosl_test` 实际是 x86-64 可执行文件，未执行，也不能用作 S31 验证依据。

P0 的最小链接工程必须覆盖 RTC 初始化、G.722 收发、RTM、JPEG 发送和停止/销毁路径。只链接版本查询函数不足以验收。产出 ELF、map 和依赖清单，确认没有 x86 成员、Xtensa 库、重复 AOSL 或未解析 libc/libgcc 符号，再上板检查这些路径是否有非法指令或生命周期问题。

## 4. 板级硬件映射

以下是官方用户指南提供的 V1.1 硬件事实；寄存器配置、时序与电源顺序以固定 BSP 和原理图复核为准。

| 模块 | 已确认硬件 | 平台适配 |
| --- | --- | --- |
| 主控/内存 | ESP32-S31-WROOM-3；16 MB Flash、16 MB PSRAM | 新建 S31 defaults，重新确认 PSRAM 模式/速度、cache、内存预留和分区 |
| 音频 | ES8389、双麦克风、两路 NS4150B 功放；codec I2C SDA0/SCL1，PA GPIO7 | 新增 ES8389 全双工驱动；先解决下述文档/BSP 总线格式差异 |
| 音频线 | MCLK2、SCLK3、LRCLK4、SDOUT5、DSIN6；BSP 默认 48 kHz/16-bit/stereo | 文档要求两方向 TDM，BSP 却调用 std mode；必须核对实际连线、codec 寄存器和波形 |
| 输入/指示 | GPIO42 电阻分压 PLAY/SET/VOL-/VOL+；GPIO37 WS2812 | ADC 按键分档、去抖/长按、音量持久化及状态灯 |
| 显示/触摸 | 4.3 英寸 SUB3，800×480 RGB565，PCLK 26 MHz；GT1151 与 codec 共用 I2C0 SDA0/SCL1，reset/IRQ 为 NC | 新 RGB panel/LVGL adapter 和 GT1151 驱动；当前 BSP 未使用已路由的 LCD SPI 控制线 |
| 摄像头 | DVP；示例支持 OV3660 或 SC101/SC101IOT；SCCB 复用 I2C0，reset/PWDN 为 NC | 读取实际 sensor ID，选匹配驱动与输出格式 |
| 调试 | UART0 TX58/RX59，经板载 USB-UART；独立供电 USB 口只供电 | 首轮用 UART0 烧录和日志 |
| 可选外设 | microSD/NAND 共用 GPIO20–25 | 首轮不初始化，后续按需要启用 |

固定 BSP 的 RGB 时序为 HSYNC pulse/back/front = 1/40/20，VSYNC = 1/10/5，默认 0 度旋转；framebuffer 放 PSRAM。backlight 和 disp_en 配置为 NC，LCD_CS38/MOSI60/SCK61 虽已路由但当前驱动未使用，不应凭空增加 SPI 命令或背光 GPIO。LCD 具体控制器型号尚未在这些 BSP 文件中确认。

**首要差异：官方用户指南写明 ES8389 ADC/DAC 均须 TDM，Pin4 MCLK 可复用为 TDMIN；固定 BSP 却使用 `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(..., I2S_SLOT_MODE_STEREO)` 和 `i2s_channel_init_std_mode()`，codec 配置为 `master_mode=false`、`use_mclk=false`、`no_dac_ref=false`。**不能宣称该 demo 已提供可直接复用的 TDM 初始化。应核对 ES8389 驱动寄存器、板卡连接和实际 BCLK/WS/数据波形，通过录放验证确定总线配置。

必须在冻结 `board_config.h` 前确认：

1. 解决上述音频格式差异，确认时钟来源、slot 数/位宽/mask、实际采样率、通道映射、Pin4 配置和功放启停顺序。
2. 实际子板与固定 BSP 的 800×480/时序/NC 配置一致；GT1151 地址和轮询方式可用，确认无需额外控制器命令。
3. 摄像头型号、供电和 FOURCC；SCCB 共用总线的生命周期，以及 LCD_CS 与摄像头 GM_FK 共用 GPIO38 时的状态。
4. GPIO38/39/40/60/61 等启动复用管脚的外设上电状态；供电是否能承受 Wi-Fi、背光和双功放同时工作。

双麦硬件不代表已有本地 AEC、波束成形或唤醒算法。首轮只向 SDK 提供经过验证的单声道输入，避免把两个麦克风通道直接混成回声参考。

## 5. 工程组织与文件改动

建议保留已有目标仓库的 `.git`、README、LICENSE、`.gitignore` 和原始 tgz。按清单导入当前项目的公共源码及必要组件，排除 `.git`、build、releases、sdkconfig、managed_components、缓存、设备配置和 S3 预编译库。新增 ignore 规则覆盖构建输出和本地依赖输入；不提交用户提供的第三方二进制。

```text
mybot-esp32s31/
  CMakeLists.txt
  sdkconfig.defaults
  sdkconfig.defaults.esp32s31
  main/
  components/
    mybot_stack/
      mybot_sdk/                  可修改 wrapper；mybot/ 快照不变
      aosl/                       源码快照不变
    mybot_platform/
      boards/esp32-s31-korvo-1/
        board.cmake
        board.c
        board_config.h
        sdkconfig.defaults
      ports/
        aosl/                     外置 S31 CMake/HAL/config，component name = aosl
        agora_rtc/                新包 wrapper，component name = agora_rtc
          vendor/                 本地导入的新 header/lib，gitignore
      src/drivers/
        audio/korvo_s31_codec_audio.c
        input/                    ADC keys / GT1151
        display/panels/            RGB panel 生命周期
        display/adapters/lvgl/     RGB LVGL adapter
        video/korvo_s31_camera_video.c
  ci/
  scripts/
  docs/
```

| 位置 | 具体调整 |
| --- | --- |
| 根 CMake / boards.cmake | 默认板为 `esp32-s31-korvo-1`；profile 设置 `MYBOT_BOARD_TARGET=esp32s31`；注册新的 AOSL/RTSA wrapper，只保留每个 component name 的一个实现 |
| mybot_sdk/CMakeLists.txt | 放开 S31 target，继续编译原 SDK 源码；保留公开接口边界，不改 `mybot/` |
| mybot_platform/CMakeLists.txt | 支持 S31；按板能力收集 codec/display/video 依赖，移除 CoreS3 专属判断对新板的限制 |
| board.c / board.cmake | 完成硬件准备、平台注册、联网/配网前置条件和 shutdown；统一持有 codec/touch/camera 共享 I2C0；声明分区、必需/禁止配置和外设依赖 |
| defaults / Kconfig | 新建 S31 默认项；CoreS3 的 GC0308 select、DVP 8 KiB 和 S3 内存预留不进入新板默认项 |
| builder / CI | 使用显式显示/视频 capability，取代摄像头文件名识别；增加 dry-run、中英文、视频关闭/开启矩阵 |
| 文档/来源 | 更新中英文板卡、UI、视频文档及 CHANGELOG、VENDORED_SOURCES、THIRD_PARTY_NOTICES，固定版本和许可证 |

### AOSL 外置适配

现有 AOSL 是源码构建，`esp32-s3` HAL 多数采用 GCC atomics、FreeRTOS/pthread、lwIP 和 heap_caps，没有发现必须整体重写的 Xtensa 汇编依赖。限制主要在硬编码平台名、config 路径和需要验证的 OS ABI。

外置 wrapper 引用不可变快照的通用源码/构建清单，移除需要替换的源文件和旧 config include，添加 S31 HAL/config；明确 include 优先级，避免重复实现。替换硬编码的 OS 名称，验证 `__atomic_*`、静态 mutex 大小断言、句柄、栈大小单位、优先级和线程退出流程。不可只把 `CONFIG_PLATFORM` 改成一个不存在的目录名。

若依赖需要 AOSL 快照内部没有提供的接口，应暂停该适配点并确认匹配的完整上游版本；不能向 SDK 私有实现中添加平台接口。

## 6. 外设和依赖方案

### 音频与提示音

当前精简 `esp_codec_dev` 没有 ES8389 实现，需接入与固定 BSP 匹配的官方版本/源码和许可证，不能用 ES8388 驱动替代。平台适配层对 SDK 始终提供 **16 kHz、mono、signed 16-bit PCM，参数单位为帧数**。先解决文档与 BSP 总线格式差异，再验证 native 16 kHz 是否可用；若需沿用 BSP 的 48 kHz/stereo，则在平台层完成重采样、通道提取和播放映射，多通道/TDM 处理也留在平台层。

capture/playback 独立启停，共享 I2S/codec 资源引用计数；提示音播放排空缓冲，功放按顺序启停；stop/destroy 有界、可重试。

当前 `esp_audio_codec/lib/esp32s3/libesp_audio_codec.a` 不能用于 S31，而 announcement 的 Ogg/Opus decoder 依赖它。新 RTSA 的 G.722 收发能力不能替代这个提示音 decoder。选择顺序：

1. 取得并验证支持 S31/RISC-V ABI 的官方音频解码包。
2. 如无可用包，首轮将现有提示音离线转换成 16 kHz mono PCM，嵌入 Flash，播放缓冲按需放 PSRAM；重新计算固件和分区占用。
3. 如需继续保留 Ogg/Opus 压缩资源，再集成源码 libopus，并验证耗时、内存和许可证。

不能调用 RTSA 内部未公开的 Opus 符号。

### RGB、GT1151 和共享 UI

现有 LVGL 是可重新编译的 C/C++ 源码，共享 `lvgl_view.cc` 可复用。当前 SPI adapter 的 byteswap 和 transfer callback 回收逻辑不适用于 RGB framebuffer，需要新增 RGB adapter，处理 VSYNC、buffer ownership、flush 完成、正确字节序及回调销毁顺序。

panel 驱动负责实际存在的控制资源和 RGB 时序，NC 管脚按固定 BSP 处理；通过 display.cmake 自动接入共享 LVGL，不增加手动启用 LVGL 的选项。先用软件绘制，后续再评估 PPA。GT1151 先映射现有输入事件；需要操作 LVGL 控件时，补齐本地 port 的 indev 集成。

800×480 RGB565 的一个 framebuffer 为 768,000 字节，双缓冲为 1,536,000 字节，另加 LVGL draw buffer、资源缓存和相机占用。缓冲数量按撕裂/性能验证决定，不能只凭 16 MB PSRAM 容量认定并发带宽充足。

### 摄像头与硬件 JPEG

现有 `esp_video` 和 `esp_cam_sensor` 已裁剪为 S3/GC0308，必须采用固定 BSP 对应的 S31-capable 版本和实际 sensor 驱动，不能仅删 target guard。首轮只验证实际安装的一款传感器；仅在两种摄像头共享同一电气/分区/组件契约时才加入运行时识别。

S31 不链接 `esp_new_jpeg/lib/esp32s3/libesp_new_jpeg.a`。采用 IDF 的 `driver/jpeg_encode.h`，通过 `jpeg_new_encoder_engine()`、`jpeg_encoder_process()` 和 `jpeg_alloc_encoder_mem()` 管理硬件编码及缓冲。

**像素次序必须明确：**IDF 的 `JPEG_ENCODE_IN_FORMAT_YUV422` 对应 **YVYU**，现有 CoreS3 是 **YUYV**，不能直接连接；OV3660 的 RGB565_BE 也需要核对编码器字节序。优先选择两端共同支持的格式，否则明确转换成本，并用彩条/实拍检查颜色。

新驱动实现公开 `mybot_video_ops_t`。init 只准备资源，start 创建 worker，stop 停止采集并等待回调，destroy 不得释放仍被回调引用的 buffer。初期沿用当前最高 1 fps 上传策略；源帧率、采集帧率、编码/上传帧率和 DMA 缓冲分别记录，避免仅通过降上传频率掩盖采集开销。

### PSRAM 与分区

S31 SoC 明确支持 PSRAM DMA。图像、RGB framebuffer、较大音频 FIFO 和一般应用缓冲优先放 PSRAM，但必须遵循各驱动的 DMA 对齐/cache 一致性规则；JPEG 缓冲优先使用官方 allocator。内核对象、必须内部驻留的 DMA 描述符/ISR 状态以及涉及禁用 cache 时运行的对象或栈，按 IDF 要求留在内部 RAM。

不复制 S3 的“DMA 数据全放内部”、8 KiB DVP 缓冲、96 KiB 预留等经验参数，也不直接照抄 factory demo 的全部 PSRAM、XIP 和大 UI cache 设置。记录 internal/DMA free、largest block、PSRAM free 和峰值，按音视频并发结果调优。

使用板级明确的 16 MB 分区表；从当前 16 MB 布局复核 NVS、OTA/应用槽及资源需求。无需 factory demo 的 coffee_pjpg 资产分区；PCM 备用提示音会增大应用镜像，应纳入槽位计算。

## 7. 分阶段实施与通过标准

| 阶段 | 工作与产物 | 通过标准 |
| --- | --- | --- |
| P0：依赖/硬件基线 | 固定 BSP/依赖；解决 ES8389 文档/BSP 格式差异并核对实际外设；临时最小工程链接本地 S31 AOSL + MyBot + 新 RTSA，保留 map | 确认架构、ABI、libc 和符号来源；无错误架构对象；关键 RTC/RTM/音视频路径有链接与上板证据；参考 demo 外设结果单独记录 |
| P1：新工程骨架 | 选择性导入公共代码，新 board 四文件、S31 defaults、分区与 wrappers；基础启动、PSRAM、UART/Wi-Fi | 目标是 esp32s31；PSRAM/Flash/console 与板一致；取得 IP；HTTPS 证书验证和设备身份流程正常 |
| P2：语音闭环 | 经验证的 ES8389 总线、ADC 按键、提示音、配对和会话 | 16 kHz/60 ms 路径经验证；双向语音、音量持久化、提示音排空、联网恢复及重复启停正常 |
| P3：显示与触摸 | RGB panel/adapter、GT1151、共享 LVGL | 中英文 UI、颜色/方向、触摸/长按、实际供电/显示使能行为（NC 按 BSP 处理）、音频同时运行正常，flush/回调生命周期正确 |
| P4：视频 | 实际 sensor、S31 DVP、硬件 JPEG、公开 video ops | 颜色与 FOURCC 正确；服务端收到 JPEG；上传 cadence 可测；音视频并发及 stop/restart 正常 |
| P5：回归交付 | 双语/视频开关构建、builder dry-run、故障注入、长期运行与资源记录、文档/许可证 | 无持续内存泄漏或死锁；配网/重连、HTTPS、输入、音频、UI、视频和失败清理完成实机回归；编译与硬件验证分别标注 |

优先交付顺序：**依赖可链接 → 联网和语音可用 → 显示触摸 → 摄像头 → 并发与长期稳定性**。如 P0 发现 binary ABI 或未公开接口阻塞，先解决该点，避免在不稳定依赖上堆叠外设代码。

## 8. 移植完成后的开发构建命令

以下是计划支持的命令，**当前目标目录尚未具有相应源码和 board profile，暂不能直接执行成功**。开发直接调用 `idf.py`；MyBot board profile 决定 SoC，不需要另设一套板型开关。

```sh
cd /Users/shangjinlong/Desktop/workspace/github/mybot-esp32s31
source /Users/shangjinlong/esp/esp-idf/export.sh

idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" \
  build
```

相机路径验证后，使用独立输出目录启用视频，便于保留无视频基准：

```sh
idf.py --preview -B build/korvo-s31-video \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31-video/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults;ci/video.defaults" \
  build
```

上面的 60 ms 配置是沿用现有方案的初始选择，须在 P0/P2 确认。首次使用全新 build/sdkconfig，不复用 CoreS3 缓存。当前根 CMake 会前置 common defaults、追加 board defaults，IDF 还会读相应 `.esp32s31` 文件；实施时整理配置归属，避免互相覆盖。修改 defaults 不会自动覆盖已有 sdkconfig，应通过受控重新生成并检查实际配置验证。

批量交付时再使用更新后的 builder 构建中英文、生成 size 和 merged image；builder 需要支持 preview 和显式能力。开发构建无需调用 Python 批量脚本。

## 9. 验证记录与本次交付状态

实施时至少完成：

- IDF v6.1、S31 编译器版本、依赖锁定、RTSA 哈希、ELF/map 与组件来源检查。
- 中英文构建、视频开/关、clang-format/SPDX 与 `git diff --check`；检查生成的 sdkconfig、镜像/分区容量。
- 生命周期故障注入：初始化中途失败、部分清理、超时、重复 start/stop、回调顺序、资源释放。
- 实机配网、断线重连、HTTPS、配对、双向音频、音量保存、提示音、ADC/触摸、LCD 颜色/方向、摄像头颜色/帧率/服务端接收和并发音视频。
- 每核心 CPU、internal heap/DMA 最大块、PSRAM 和任务栈水位；启动、入会、显示刷新、视频开始、停止后的资源变化。

本次已做：源/目标项目只读检查、RTSA 静态审计、官方硬件文档及固定 BSP 关键实现/头文件/manifest 核对和方案整理。环境中已有 IDF 6.1 S31 hello_world 编译结果，这只证明工具链可用。尚未做：新 MyBot 工程编译、新 RTSA 完整链接、参考 demo 运行和 Korvo-1 实机测试。
