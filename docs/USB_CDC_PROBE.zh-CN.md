# S31 原生 USB CDC 验证

`CONFIG_MYBOT_USB_CDC_PROBE` 是默认关闭的诊断功能。开启后，J2 原生 USB 尝试枚举为
`MyBot S31 USB Probe`，提供一个二进制原样回显的 CDC 接口。本次启动只运行 USB 和
UART 日志，不初始化 NVS、网络、显示、音频和翻译业务。启动日志带
`event=usb_probe_boot scope=cdc_only`，用于与之前同时运行翻译业务的版本对照。
它不提供 USB 音频，不将 USB 输入当作命令，也不接管 UART0 的日志与烧录。
需要用手机等外设对照验证 J2 的 Host 功能时，使用互斥的
[USB Host 枚举探针](USB_HOST_PROBE.zh-CN.md)。两种探针的 USB 角色和启动范围不同。

## 板级边界

官方 Korvo V1.1 的 J1 Type-C 接 CP2102N，J5 Type-C 仅供电，J2 Type-A 接 S31 原生
USB2 OTG。J2 的 VBUS 经 TPS2051C 输出，EN 由电阻上拉，原理图未提供 MCU 控制。
本功能不关闭该 5 V 输出，也没有伪造 VBUS 检测引脚。普通连接暂时未出现故障、USB
枚举成功或 CDC 回显成功，均不能证明两端供电连接安全。

软件验证应与板级供电/Host 角色确认分开。保留原厂 PCB 的验证方案可以使用保留数据线
及地、隔离双方 VBUS 的合适连接治具，并让板卡独立供电；普通 A→C 线还涉及 Type-C
角色识别，不能仅凭接口形状或断开一根电源线推断接法正确。
依据为[官方原理图](https://dl.espressif.com/schematics/esp32-s31-korvo-1-schematics.pdf)
的 V1.1 第 2、3 页。

## 构建与回退

所有本地构建仍使用 `build/korvo-s31` 及其 sdkconfig。首次加入此功能、现有 sdkconfig
尚无相关选项时，可附加诊断 defaults；已有显式配置以 menuconfig 为准。

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/korvo-s31 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults;ci/usb-cdc-probe.defaults" reconfigure
idf.py --preview -B build/korvo-s31 build size
```

最终配置应有 `CONFIG_MYBOT_USB_CDC_PROBE=y`、一个 CDC 接口、1024 字节 RX/TX FIFO
和 512 字节 endpoint buffer。S31 使用其唯一的 HS-capable port 0/UTMI；通过目标默认
配置选择，不能照搬 P4 的 port 1 或 S3 的 GPIO19/20。依赖固定为 `esp_tinyusb 2.4.0`
与 `tinyusb 0.19.0~3`，由 IDF 生成 lock/managed components，不手改生成配置。

本次构建前的已测试面对面固件与配置保存在忽略目录 `build/korvo-s31/before-usb-probe/`，
`backup.json` 记录每个文件的长度和 SHA-256。需要回退固件时，在该备份目录使用其原始
分段参数，保留 NVS：

```sh
cd build/korvo-s31/before-usb-probe
python -m esptool --chip esp32s31 -p PORT -b 460800 \
  --before default-reset --after hard-reset --no-stub write-flash @flash_args
```

恢复常规开发时，在 `idf.py --preview -B build/korvo-s31 menuconfig` 的 `mybot` 菜单关闭
`Enable native USB CDC loopback probe`，去掉后续配置命令中的诊断 defaults，再在同目录
构建。关闭选项后主程序不安装 USB Device 驱动。回退固件和关闭下一次构建的选项是两项
独立操作。

后续已通过安卓手机枚举的 Host 测试固件保存在
`build/korvo-s31/usb-host-probe-passed/`，包含原始分段镜像、`flash_args` 和配置；
其 `backup.json` 记录 SHA-256。可在该目录使用上面的 esptool 分段命令恢复。

## macOS 验证

测试固件通过原 CP2102N 串口烧录，分段烧录保留 Wi-Fi、绑定和语言设置。
快速试错时直接烧录现有文件，避免触发构建；在仓库根目录运行：

```sh
cd build/korvo-s31
python -m esptool --chip esp32s31 -p PORT -b 460800 \
  --before default-reset --after hard-reset --no-stub write-flash @flash_args
cd ../..
idf.py --preview -B build/korvo-s31 -p PORT monitor
```

`PORT` 使用当前 CP2102N 串口，更换 Mac 接口后名称可能变化。
串口出现 `event=usb_probe state=initialized` 只表示软件已初始化。
`mounted=1` 表示 USB 已完成配置；`dtr=1` 表示电脑打开了 CDC 数据通道。
日志同时报告 `speed=high/full`、mount/unmount、suspend/resume 和收发统计，未记录数据内容。
未监测电脑 VBUS，因此不能把 unmount 次数当作可靠的物理拔线计数。

```sh
ioreg -p IOUSB -w 0
system_profiler SPUSBHostDataType
python scripts/check_usb_cdc_probe.py --list
python scripts/check_usb_cdc_probe.py
```

使用已加载 IDF 环境的 Python（包含 pyserial）。脚本仅打开名称、序列号、VID/PID 均
匹配的 `/dev/cu.usbmodem*`；不会打开 `usbserial`/CP2102N。多个匹配设备时需要明确
`--port`，指定端口仍会检查身份。`--list` 只枚举，不打开任何串口。

默认进行两轮关闭/重开，验证随机二进制、控制字节、64/512 包边界和大于固定缓冲区的
64 KiB 数据。读写交错进行并有总超时，任何缺字节、重复、损坏或额外字节均失败。
每轮开始前清理电脑侧旧输入；挑战开始后不通过丢弃异常字节来掩盖错误。

设备端使用单个 worker 和 512 字节 pending 缓冲；部分写入只推进已接受的偏移，FIFO
满时停止继续取 RX。`tx_queued` 仅表示写入 USB FIFO，端到端成功以电脑逐字节比较为准。
mount、DTR 变化和 detach 会丢弃应用旧 pending；FIFO 清理无法撤销已经提交给 endpoint
或到达电脑的数据。初始化/回收失败后本次启动不重复安装，日志提示重启恢复。

CDC 回显通过仅验证原生 USB 枚举及 Bulk 数据链路；会议声卡仍需另行验证 UAC 等时端点、
双向 PCM、时钟漂移、音频路由和 AEC。

## 已初始化但未枚举

`No Device descriptor provided, using default` 及对应 FS/HS/qualifier 提示表示使用组件
内置描述符，是本诊断的预期行为。`Driver installed` 与 `state=initialized` 表示软件
初始化返回成功；`mounted=0` 表示主机尚未完成 `SET_CONFIGURATION`，不能将它当作
物理插线或总线 reset 的计数。没有枚举出的 CDC 接口时，反复运行回显脚本不会发起 USB
枚举，也不能检测数据线的电气连续性。

诊断第二版的初始化行带 `diag=2`。它增加以下只读信息，不改变控制器配置：

- `reset_events/setup_events/xfer_events` 是 TinyUSB 报告的总线事件计数；回调可能在 ISR
  中执行，只累加计数，不打印日志。它们不是物理电压或原始复位脉冲的测量。
- `stack_inited/stack_connected/stack_mounted` 来自 USB 栈本身，区别于应用观察到的
  mount 回调。`stack_connected` 在收到初始控制请求后才有意义。
- `usb_probe_phy` 和 `usb_probe_controller` 记录时钟、实际 Device/Host 模式、软断开、
  地址、内部会话状态、帧号及线路状态；时钟关闭时跳过控制器寄存器访问。
- `usb_probe_registers` 提供对应的非破坏性寄存器快照；不读取 FIFO 或写任何寄存器。
  初始化时另外输出一次中断分配表，可检查 `USB_OTGHS` 是否已分配。

若事件计数始终为零，需要结合时钟、中断分配、连接角色及物理链路继续定位；不能只凭
这一项判断线缆坏。若出现 SETUP 或非零设备地址，说明已经进入控制传输；有地址而
`stack_mounted=0` 时，应检查描述符/配置阶段。内部 `b_session_valid` 不是接口 VBUS
电压测量，`enum_speed_raw` 的复位默认值也不能证明 High-Speed 枚举成功。

普通 A→C 线通常按 A 端为主机/供电侧连接。把 A 插在原厂板 J2、C 直接插 Mac 时，
不能据此假定 Mac 已处于所需的 Host 数据角色；S31 的 USB Device 固件不能改变线内
的 Type-C CC 接线。需先明确电脑 Host 角色及双方 VBUS 隔离，再定位 PHY、中断或
控制传输。当前 `mounted=0` 的结果不证明应更换描述符、修改 LCD 引脚或升级 USB 库。
