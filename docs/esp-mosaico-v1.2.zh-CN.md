# ESP-Mosaico CoreBoard V1.2

当前默认目标板为 `esp-mosaico-v1.2`，后续功能以它为设计基线。固件复用现有媒体翻译、
面对面翻译、RTM 字幕、Wi-Fi 配网及设备绑定。会议互译同时接入电脑听译和板载麦克风译音回传电脑。

## 硬件与接口

| 项目 | 当前配置 |
| --- | --- |
| SoC / SDK | ESP32-S31 / ESP-IDF v6.1 preview |
| 存储 | 16 MB NOR、16 MB Octal PSRAM；128 MB NAND 暂不使用 |
| 显示 | 480×480 CO5300，40 MHz QSPI，RGB565，局部刷新 |
| 触摸 | CST9220，与 codec 共用板级 I2C0（SDA56 / SCL3） |
| 音频 | ES8311，16 kHz / 16-bit Philips I2S stereo，MCLK256；SDK 收发 mono PCM |
| I2S（MCU 方向） | TX52、RX40、BCLK37、WS49、MCLK54；功放使能45 |
| 电源 | GPIO60 低使能 VCC_3V3；GPIO57 开漏释放关机信号 |
| USB | Type-C 原生 HS UTMI；翻译固件提供 UAC2 声卡与 CDC 日志；声卡同时接收电脑原声与回传设备麦克风的译音 |
| 操作键 | AI GPIO7 低有效；BOOT 保留给 ROM 下载模式 |

原理图的 `I2S_DOUT` / `I2S_DIN` 来自 codec 视角，不能据此交换 MCU 的 TX/RX。
不使用 ES8389、TDM4 或无 MCLK 配置。

启动先读取 eFuse `USER_DATA` 的前 16 位，再操作任何供电、功放或 I2C 引脚。
`0x0100`（V1.0）使用不同引脚映射，固件拒绝启动；`0x0101`（V1.1）与 `0x0102`
（V1.2）使用官方 V1.2 映射，V1.1 会明确记录兼容接入。eFuse 读取失败同样拒绝继续。
对未烧录或未知值，本项目按已选择的 V1.2 目标板继续，并打印
`result=degraded policy=assume_v1.2_pin_map`。这是项目允许的降级策略；官方 BSP 对未知值
直接拒绝，降级日志不表示已经识别或验证了该硬件版本。

VCC 使能保留官方软启动：若早期启动阶段已将 GPIO60 配置为低电平输出，则接管当前
开启状态；否则先保持高电平关闭，再用 LEDC 低速 timer1/channel1、100 kHz、8-bit
占空比从 255 降到 0，等待约 50 ms 渐变完成，最后恢复为 GPIO 低电平输出。
随后保留 10 ms 稳定等待，再初始化 I2C。软启动失败会尝试恢复高电平关闭，不会直接
跳过渐变强行上电；I2C 初始化重试不重复已完成的上电过程。此过程移植自固定版本官方
BSP，尚不能替代浪涌、电压或启动时序的实测结果。

音频采集与播放共用既有生命周期实现，保留有界 I/O、独立启停、延迟回收及音量保存。
默认启用 ESP-SR 端侧 AEC：ES8311 在同一条 I2S 上同步返回左声道麦克风和右声道 DAC
参考，平台音频层处理后向 SDK 返回干净的 16 kHz mono PCM。menuconfig 仍可切换到
Cloud AEC；该模式保留原有麦克风/播放参考交错上传逻辑。端侧 AEC 的声学效果仍待实机验收。
触摸通过 INT6 通知读取新报文，在报文之间保留触点，避免反复读取已 ACK 数据。
根据用户本机点击日志，触摸 Y 方向与 CO5300 显示相反，使用公共驱动的 `mirror_y`
将坐标映射为 `479 - y`；X 方向不变。
界面顶部内收 60 px，主体内收 32 px，为玻璃圆角留出可见区域。
屏幕使用 PSRAM 中的 LVGL 绘制缓冲和独立内部 DMA 暂存区，避免超时后重用仍在传输的内存。
当前使用 40 行刷新缓冲（两块各 38,400 字节）。用户已确认该版本的字幕横线消失、
显示正常；此配置作为当前 Mosaico 显示基线。

## 构建与烧录

开发只构建中文版本，目录固定为 `build/mosaico-v1.2`。本次生成的是
Mosaico 固件。从其他板型或独立诊断切换时，先备份需要保留的生成固件，再通过 IDF
`fullclean` 清理旧构建；不要混用旧 `sdkconfig`，不要手改生成配置。

中英日字幕使用压缩的 2 bpp 字库。`partitions/v2/16m.csv` 的两个 OTA 槽均为
`0x470000` 字节，起点分别是 `0x20000`、`0x490000`；未使用的 `assets` 分区为
`0x900000` 起的 7 MiB。每个 OTA 槽比旧布局增加 512 KiB，仍要求至少 300 KiB
固件余量。从旧布局升级需使用下方完整的 `idf.py flash`，同时写入分区表和初始 OTA
状态，不能只写应用 BIN；NVS 的位置和大小未变，可保留配网、绑定和音量。

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/mosaico-v1.2/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

首次从 UAC 固件烧录：关机，按住 BOOT，开机后松开 BOOT。检查 Mac 的实际下载端口，
用它替换下面的端口（之前用户的端口为 `/dev/cu.usbmodem144101`）：

```sh
ls /dev/cu.usbmodem*
idf.py --preview -B build/mosaico-v1.2 -p /dev/cu.usbmodem144101 flash
```

启动后 Type-C 会重新枚举为 `MyBot Mosaico` 复合设备，包含 UAC2 音频和 CDC 日志；端口号可能变化。监视应用日志：

```sh
idf.py --preview -B build/mosaico-v1.2 -p RUNTIME_PORT monitor
```

应用 CDC 在板级准备时启动，无法提供此前的 ROM / bootloader 日志。CDC 不实现自动进入
ROM 下载，后续烧录同样可用 BOOT 手动进入。USB 序列号为 `MOSAICO-` 加设备 MAC，
每台板的运行时端口由 macOS 实际枚举决定，不能沿用旧固件的固定串口名称。
拔掉 USB 不影响媒体／面对面独立运行；会议翻译依赖 USB 音频连接。
烧录使用 `partitions/v2/16m.csv`，与 UAC 诊断分区不同；无需执行整片擦除。

会议互译将电脑下行音频送往 LISTEN Agent，在板载喇叭播放译音；板载麦克风送往
SPEAK Agent，其译音通过 USB 麦克风回传电脑。媒体与面对面继续使用原有板载音频。
枚举、收发计数和并发验证见 [USB 声卡与日志串口](MOSAICO_USB_AUDIO.zh-CN.md)。

## 首次使用与验收

1. 没有 Wi-Fi 凭据时，在屏幕提示的 `mybot-xxxx` 热点配网，浏览器打开 `http://192.168.4.1`。
2. 联网后按屏幕提示在 MyBot 网页绑定这台新设备。Mosaico 的设备身份来自自己的 MAC，
   不继承旧固件的绑定。
3. 媒体翻译默认中文译英文；选择源语/目标语，点击开始，核对字幕与喇叭译音。
4. 面对面模式使用两路 RTC 和一个 RTM，连接后点击“我说中文”或“对方说英语”选择当前
   麦克风方向，再分别测试两种语言。默认连接后不发送麦克风原声，等待选择方向。
5. 测试暂停/继续、结束、再次启动、音量调整以及重启后音量保存。方屏可打开完整字幕和本次历史。

翻译开始和停止只通过触摸屏操作。AI 键是设备维护键：短按小于 3 秒无动作，按住 3 秒
请求停止 MyBot 并重新配网，继续按住到 10 秒会擦除本地 NVS、重启并重新进入配网。
恢复出厂不会擦除固件、OTA、分区表、PHY 校准或 assets，也不会解除服务端绑定。
首次默认音量为 100，之后使用 NVS 保存值。

会话恢复记录采用紧凑格式，只保存实际字符串，减少 NVS 覆写时新旧副本的空间占用。
升级兼容读取旧记录，正常使用分段 `idf.py flash` 即可保留 Wi-Fi、绑定和音量；无需擦除 NVS。

## 验证范围与来源

用户已确认独立 48 kHz UAC 诊断的麦克风和喇叭正常。翻译固件已确认的 Mosaico 功能为
PSRAM 栈修正版（`e279926`）的基础双向会议：电脑听译到喇叭、麦克风译音回传电脑；
40 行刷新缓冲的字幕显示也已确认正常。触摸导航和启动操作另有实机日志。
媒体／面对面已实现于本板。
后续 review 修复目前只有 host 测试和中文构建结果，尚未重新真机验收。
端侧／Cloud AEC 音质、重连／长稳、USB 电流及修订后的 VCC 软启动仍需单独测量或验证。
SPI 刷新发生超时后会停止继续提交，保留尚未完成的 DMA 缓冲；本版显示故障需要重启恢复。
当前宿主/构建结果记录在 [s31-validation.md](s31-validation.md)。

- [官方用户指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide.html)
- [CoreBoard V1.2 原理图](https://dl.espressif.com/AE/Mosaico/SCH_SCH_ESP-Mosaico_CoreBoard_V1_2_1_2026-09-24.pdf)
- [固定版本官方 BSP](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/8ce87a7ef130f946de4f84f6a5a20db1d8982605)

采用必要引脚、eFuse 版本映射、供电软启动和 CO5300 初始化序列，保留其 Apache-2.0 版权。没有引入要求 IDF 6.2 的整套 BSP，
官方 codec 1.5.9、RTSA 和 AOSL 上游源码保持不变；AOSL 的组件包装及 S31 适配器由工程维护。
CO5300 2.2.0、CST9220 0.1.0 由组件管理器固定下载。
