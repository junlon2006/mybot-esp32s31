# S31 USB Host 枚举验证

`CONFIG_MYBOT_USB_HOST_PROBE` 是默认关闭、与 CDC Device 探针互斥的诊断选项。
开启后本次启动只运行 USB Host 和 UART 日志，屏幕、Wi-Fi、音频和翻译业务均不启动，
不初始化或擦除 NVS。它用于验证原厂 J2 Type-A 在 Host 角色下能否与外部设备通信。

## 用安卓手机验证

1. 保留板卡供电，以及 Mac 到板卡 **J1 Type-C USB-UART** 的烧录/日志连接。
2. 拔掉 J2 到 Mac 的线。诊断固件中的 J2 是 Host，应接 USB 外设。
3. 直接烧录已构建的 Host 探针，不触发重新编译。在仓库根目录运行：

   ```sh
   cd build/korvo-s31
   python -m esptool --chip esp32s31 -p PORT -b 460800 \
     --before default-reset --after hard-reset --no-stub write-flash @flash_args
   cd ../..
   idf.py --preview -B build/korvo-s31 -p PORT monitor
   ```

   `PORT` 是 CP2102N 串口，例如 `/dev/cu.usbserial-1430`；以当前枚举结果为准。
   此命令使用分段镜像，保留已有配网、绑定和语言设置。
4. 用现有 A→C 数据线将 **J2 Type-A 接手机 Type-C**，解锁手机。在手机 USB 用途中
   选择“文件传输 / Android Auto”（各品牌名称不同）。不需要开启 USB 调试或授权 ADB。
5. 观察串口中的准备完成、设备枚举、VID/PID、速度、配置描述符和断开事件。
   拔掉手机后应能再次接入并枚举；切换手机 USB 用途也可能触发重新枚举。

对应日志关键字为 `event=usb_host_ready`、`event=usb_host_device`（含 `vid/pid/speed`）、
`event=usb_host_descriptors_complete` 和 `event=usb_host_disconnected`。
`event=usb_host_error` 会标明失败的操作；枚举尚未完成的错误由官方 Host 库输出。

手机可能从板卡取电，充满电不能保证没有额外负载。供电不足时的断连或复位会使本次
验证无法下结论。机械移动硬盘的启动电流会引入更多变量，不作为首选对照。

## 结果的含义

手机显示充电、驱动安装成功或打印等待日志，都不代表 USB 数据通信通过。成功读取
有效的设备和配置描述符，才说明真实的枚举与双向控制传输已发生。手机 USB 角色、
线缆、供电和描述符兼容性均可能影响结果，单次失败不能证明板卡损坏。

探针使用官方 Host 库的标准枚举流程和默认 PHY 初始化。应用仅读取库缓存的描述符，
不安装存储、MTP、ADB 或音频类驱动，不读写手机或硬盘中的文件，也不控制实际取电电流。
较大的复合设备描述符使用 1024 字节控制传输缓冲。诊断只面向直连的一台外设。

Host 验证通过不代表 **Mac Host → S31 Device** 连接、CDC 数据吞吐或 UAC 音频已经通过；
后者仍需按 [CDC 探针说明](USB_CDC_PROBE.zh-CN.md) 验证对应角色和链路。

## 配置与恢复

所有本地构建使用原来的 `build/korvo-s31`。已有 sdkconfig 时，defaults 不会覆盖显式值。
在 `idf.py --preview -B build/korvo-s31 menuconfig` 中：

- `mybot`：先关闭 `Enable native USB CDC loopback probe`，再开启
  `Enable native USB Host enumeration probe`。
- `USB-OTG` / USB Host 配置：将 `USB_HOST_CONTROL_TRANSFER_MAX_SIZE` 设为 `1024`。

然后构建：

```sh
idf.py --preview -B build/korvo-s31 build size
```

新建配置的诊断默认值见 `ci/usb-host-probe.defaults`。正式固件保持两个探针均关闭。
恢复翻译业务时关闭 Host 探针，在同一目录重新构建并烧录。

本次切换前的 CDC diag=2 固件保存在忽略目录 `build/korvo-s31/before-usb-host-probe/`，
`backup.json` 记录备份文件的 SHA-256。可按其原始分段参数恢复并保留 NVS：

```sh
cd build/korvo-s31/before-usb-host-probe
python -m esptool --chip esp32s31 -p PORT -b 460800 \
  --before default-reset --after hard-reset --no-stub write-flash @flash_args
```

更早的已测试面对面翻译固件仍在 `build/korvo-s31/before-usb-probe/`，其回退方法见
[CDC 文档](USB_CDC_PROBE.zh-CN.md)。不要对构建目录运行会删除这些备份的清理操作。

参考：ESP-IDF v6.1 的 `examples/peripherals/usb/host/usb_host_lib` 通用枚举示例；
官方工厂示例的 [HID Host 初始化](https://github.com/espressif/esp-dev-kits/blob/5c06bf1835329d6525f316475f7218aab6acfe79/examples/esp32-s31-korvo/examples/factory_demo/main/factory_demo_usb_hid.c#L279)。
本项目未导入工厂示例的摄像头或应用界面。
