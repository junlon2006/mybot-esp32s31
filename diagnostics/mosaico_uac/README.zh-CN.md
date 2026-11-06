# Mosaico V1.2 双向 USB 音频最小测试

仅适用于用户确认的 **ESP-Mosaico CoreBoard V1.2**。此测试镜像不能烧录到 Korvo。
USB 产品名为 **Mosaico UAC Test**；macOS 声音列表使用音频接口名 **usb uac**，
提供 48 kHz / 16-bit / 双声道输入和输出。
物理麦克风为单声道，复制到两个 USB 输入声道；USB 左右声道混音后送板载单扬声器。
初始 Codec 音量为 100（最大，默认曲线对应 0 dB），麦克风增益为 24 dB，支持主机静音和
软件音量衰减。启动时先尝试播放两声本地提示音，再启动 USB，用于区分本地播放与 USB 路径。

2026-11-05：用户已确认设备播放有声，状态显示双向 USB 音频持续传输，播放/采集 I/O
错误及播放队列丢块为零，观察期间反馈与麦克风 USB 错误计数不再增加。通过版本的镜像、
分段参数、配置和校验值保存在 `build/korvo-s31/mosaico-uac-passed/`。
这是短时间录放验证，尚未验证长期时钟漂移、AEC 或会议翻译。

本程序只启动电源、ES8311/I2S 和 UAC，不启动屏幕、Wi-Fi、翻译、存储或文件系统。
不访问 NVS、OTA 数据或外部 NAND。屏幕不显示业务界面是正常现象。
USB 只提供音频接口，没有 CDC 串口；日志仍走 UART0（需要另接调试工具才能读取）。

## 烧录

当前出厂应用只有 USB 声卡，烧录需要手动进入原生 USB ROM 下载模式：

1. 将设备关机，按住 **BOOT**，再开机，随后松开 BOOT。
2. 保持 Type-C 数据连接，在 Mac 上查看新增端口：

   ```sh
   ls /dev/cu.usbmodem*
   ```

3. 在仓库根目录、已激活 IDF 环境中直接烧录（替换实际端口）：

   ```sh
   idf.py --preview -B build/korvo-s31 -p /dev/cu.usbmodem实际编号 flash
   ```

这会写入测试用 bootloader、分区表和应用。若需烧录前自动备份，可改用
`python diagnostics/mosaico_uac/flash_test.py --port /dev/cu.usbmodem实际编号`。
该可选脚本先验证当前构建属于本测试，再备份 NOR flash 前 1 MiB，备份成功后才写入三个测试段。
这个窗口覆盖本测试 bootloader、分区表和整个应用分区；测试应用不写窗口之外的数据。
备份保存在 `build/korvo-s31/mosaico-original-*/`，附 SHA-256 和 `restore.txt` 恢复命令。
它不是整片 16 MiB NOR 或独立 128 MiB NAND 的备份。请保留该目录，避免运行 `fullclean`。
回退时再次进入 ROM 模式，执行对应 `restore.txt` 中的命令；端口可能需要更新。

脚本直接烧录现有镜像，不触发编译。不要使用旧 Korvo 的烧录参数或合并镜像。

## 验证

打开 macOS“系统设置 → 声音”，确认 **输入和输出** 都有 `usb uac`（厂商 MyBot）。
USB 设备树显示的产品名 `Mosaico UAC Test` 与声音列表名称不同。

先记录开机是否能听到两声短提示音：它不经过 USB 或 Mac 音量控制。如果提示音有声而
电脑播放无声，下一步检查 USB 播放；如果提示音也无声，优先检查 Codec/I2S/功放路径。

- 输出选该设备，先将音量调低，播放一段已知音频，确认板载扬声器出声。
- 输入选该设备，对着板载麦克风说话，确认输入电平变化；用 QuickTime 新建音频录制，
  选择该麦克风并录制约 10 秒，再回放确认是板载麦克风采到的声音。
- 最后在播放时同时录音，验证两个方向能并行工作；本测试没有 AEC。

声卡出现在列表只代表枚举成功，录放音和音质仍需要实际验证。可只读查看设备信息：

```sh
system_profiler SPAudioDataType
```

播放音乐时可直接读取设备状态，无需额外串口工具：

```sh
python3 diagnostics/mosaico_uac/read_status.py
```

该脚本使用本机 libusb，只发送一个 endpoint 0 vendor IN 请求，不申请音频接口、不卸载
系统驱动、不修改 USB 配置。当前 Mac 已安装 libusb；其他环境需自行提供该库。
`rx_packets/rx_bytes` 记录真实 USB OUT 接收，`callbacks/callback_bytes/raw_peak` 记录播放
回调和原始 PCM 峰值，`queued_blocks/played_bytes/played_peak` 记录入队和实际 I2S 写入，
`muted/gain_q15` 表示 USB 静音及软件增益（32768 为不衰减）。计数和峰值从本次开机累计；
可以间隔几秒读取两次比较变化。这里只返回计数和幅度，不返回录音或播放内容。

`fb_submitted/fb_completed/fb_errors` 记录反馈端点实际提交、完成和失败；高速模式
`fb_value` 应为 393216（48 kHz 对应每微帧 6 个采样，16.16 格式）。
`fb_interval` 在高速模式应为 4，表示每 1 ms 轮询一次反馈；全速模式为 1，同样是 1 ms。
`mic_completed/mic_bytes/mic_errors` 记录 USB 麦克风实际传输完成。
`capture_bytes` 只表示本地 Codec 采集，不能单独证明音频已传到 Mac。

此版本用于短时间连通性验证，使用固定名义采样率反馈。长期时钟漂移、重采样、生产级
丢包处理、AEC 和翻译路由不在本测试中。UART 每五秒统计收发字节、错误和播放队列溢出。

## 构建与来源

仍使用现有 IDF v6.1 和唯一的本地构建目录 `build/korvo-s31`。
根 CMake 的 `MYBOT_MOSAICO_UAC_TEST=ON` 只选取本诊断及必要依赖，不加入 MyBot、RTSA、
显示、摄像头等业务组件。`sdkconfig.defaults` 必须通过 menuconfig/kconfserver 生成配置；
已有显式 sdkconfig 值不会被默认值覆盖。

```sh
idf.py --preview -B build/korvo-s31 -DMYBOT_MOSAICO_UAC_TEST=ON \
  -DSDKCONFIG=build/korvo-s31/sdkconfig build
```

依赖固定为 TinyUSB `0.19.0~3`、`usb_device_uac 1.3.1`，锁文件为本目录的
`dependencies.lock`。复用仓库原有、未修改的 `esp_codec_dev 1.5.9` ES8311 驱动。
板级音频初始化参考官方 BSP
[`8ce87a7e`](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/8ce87a7ef130f946de4f84f6a5a20db1d8982605)
和 V1.2 原理图；相关来源的 Espressif Apache-2.0 声明保留在源文件中。

项目内兼容头仅在编译固定 UAC 组件时生效：为 TinyUSB ISR 中的通知/临界区调用选择正确
FreeRTOS API，并用名义采样率反馈替代与本测试播放队列不匹配的 USB FIFO 水位反馈。
管理组件源码保持原样。TinyUSB 使用已支持的 DWC2 PIO FIFO 音频路径，避开线性路径
在零长度 OUT 包后停止重新接收的问题。扬声器回调使用短队列隔离阻塞的 I2S 写入。

`dwc2_iso.cmake` 检查固定 DWC2 源码 SHA-256，并在构建目录生成局部修正副本：
按 S31 `DIEPTSIZn.MC` 定义，为单包等时 IN 的初次发送和重试设置 MC=1，ZLP 重试至少
安排一包；按 `DIEPTXFn` 定义，将 IN 发送 FIFO 深度限制为至少 16 个 32-bit words
（64 字节），避免四字节反馈端点被分配到无效的一字 FIFO。反馈 USB 包仍为四字节。
原管理组件文件不改动。项目内传输包装器还保证首个高速反馈即为名义采样率，
并统计实际 IN 完成事件。

配置描述符使用项目内静态副本，将高速反馈端点的 `bInterval` 对齐同版本 TinyUSB
`uac2_speaker_fb` 示例的 4（1 ms），全速保持 1；其他速度描述符同步按对应速度处理。
它针对 macOS 反馈轮询兼容性做对照。反馈数值仍是每微帧 6 个采样，不能因轮询间隔
变成 1 ms 而改成 48。音频数据端点及反馈四字节包大小不受此调整影响。

V1.2 电路复核：GPIO60 低有效使能电源，GPIO57 开漏释放；I2C SDA56/SCL3；I2S MCU
TX52/RX40、MCLK54/BCLK37/WS49；PA45 高有效。原理图的 `I2S_DOUT` 指 Codec ASDOUT，
对应 MCU RX40；`I2S_DIN` 接 Codec DSDIN，对应 MCU TX52，不能按名字交换 MCU 引脚。
HUSB320 以硬件 DRP 管理 CC/VBUS；USB 使用 UTMI port0。这些配置与
[官方 V1.2 原理图](https://dl.espressif.com/AE/Mosaico/SCH_SCH_ESP-Mosaico_CoreBoard_V1_2_1_2026-09-24.pdf)
第 4、5 页及官方 BSP 一致。

切换前的 Korvo CDC 固件和配置保存在 `build/korvo-s31/before-mosaico-uac/`。
恢复 Korvo 开发时关闭 `MYBOT_MOSAICO_UAC_TEST`，通过官方配置工具恢复该 sdkconfig，
并在同一构建目录重新配置和构建；不会自动把 Korvo 注册为 Mosaico。
