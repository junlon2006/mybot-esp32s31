# Mosaico 会议互译与 USB 日志

会议场景同时处理两个方向：

| 原声输入 | 翻译方向（默认） | 译音输出 |
| --- | --- | --- |
| 电脑经 USB 播放的音频 | 英语 → 中文，LISTEN | 设备扬声器 |
| 设备板载麦克风 | 中文 → 英语，SPEAK | USB 麦克风回传电脑 |

两路使用一个 `listen_speak` 父会话、两个 RTC 连接和一次 RTM 频道订阅。连接就绪后双方
可同时讲话，无需像面对面模式那样选择发言方向。媒体与面对面继续使用板载音频。

## Mac 设置与首次验证

1. 系统声音输入和输出都选择 **MyBot Mosaico Audio**。会议 App 有独立音频设置时，
   它的麦克风和扬声器也选择该设备。USB 格式为 48 kHz、16-bit、双声道。
2. 先在电脑播放英语人声，让 USB 输出流打开；设备会议设置选择“我的语言：中文”、
   “对方语言：英语”，再点击开始翻译。
3. 等设备进入双向会议翻译，确认设备喇叭能播放电脑内容的中文译音。
4. 用 QuickTime“新建音频录制”或其他录音软件，将输入选为 MyBot Mosaico Audio，
   关闭实时监听（QuickTime 录音监听音量设为零），开始录音。
5. 对着设备麦克风说中文。录下的 USB 麦克风内容应为英文译音。录音结束后先结束设备
   翻译，再把 Mac 输出切回内置扬声器，播放录音核对内容。
6. 最后在会议 App 中同时测试双方发言。实时字幕与历史按两路 Agent 的方向标记，暂停
   同时停止两路语音发布及译音播放，继续保留原会话，结束会清理整个父会话。

设备麦克风原声只送往 SPEAK Agent，USB 麦克风接收的是该 Agent 的译音。译音回传会有
云端处理延迟。关闭录音监听可避免录到的译音又被电脑输出送回 LISTEN。

电脑尚未打开输出流时会提示 `USB_AUDIO_NOT_READY`；保持电脑播放后重试。USB 麦克风
可在会话开始后由录音/会议 App 打开；未打开时不积压旧译音，打开后接收后续内容。
未注册完整 USB 采集与播放能力的板卡保留会议预览。

## USB 供电声明与验证边界

Mosaico V1.2 固件采用固定的 USB 2.0 总线供电策略：FS/HS 配置描述符均为
`bmAttributes=0x80`、`bMaxPower=250`，按每单位 2 mA 声明配置后最多请求 500 mA；
原生 USB 配置显式设置 `self_powered=false`，沿用默认的无 VBUS 监测 GPIO 配置。

500 mA 是固件向主机声明的供电预算。修改描述符不会实施硬件限流，也不能证明板卡在
所有负载下都低于该预算。板卡 USB Host 的向外供电额定值不能用于推算 Type-C
设备模式的实际取电。

当前没有 USB 电流及浪涌测量结果。配置前、挂起、电池充电、启动及 Wi-Fi 工作、
双向会议和满音量播放等状态的取电仍需测量；已有双向音频功能验证不能代替这些结果，
也不据此宣称 USB 电气符合性或确认板卡充电与供电路径。

## 音频与资源边界

- USB 保持 48 kHz stereo S16，SDK、Codec 和 RTSA 保持 16 kHz mono S16、60 ms。
  电脑输入经过低通滤波和 3:1 降采样；返回电脑的译音经过插值滤波和 1:3 升采样。
- 物理音频管线负责麦克风采集和 LISTEN 译音播放。SPEAK 上行使用设备扬声器的实际
  播放参考作为云 AEC 参考。USB 管线负责电脑输入和 SPEAK 译音回传，其 LISTEN 上行
  的 AEC 参考全零，两路均保留 SDK 现有 MIC/reference 交错协议。
- 会议设置的“我的语言”映射 `local_language`，默认 `zh-CN`；“对方语言”映射
  `remote_language`，默认 `en-US`。请求模式为 `listen_speak`。
- USB OUT 使用有界队列、20 ms 常态余量和水位反馈；超限时丢弃过期音频以追上当前
  语音。USB IN 随主机传输节奏发送，译音不足时发送静音，防止任务定时器与 USB 时钟
  的偏差造成持续积压。滤波在任务中执行，USB 中断只处理有界的数据复制和计数。
- USB 输入和输出队列分别拥有数据，停流、暂停、结束和断开会丢弃旧 PCM。已经交给
  USB 控制器的单个包无法撤回，其音频载荷最多为 HS 6 帧 / FS 48 帧；重试可能延后完成。
- `esp_tinyusb` 统一持有 USB PHY、任务、UAC2 与 CDC 描述符；会话启停不销毁 USB
  设备。保留已验证的 PIO/FIFO、DWC2 修正和高速 1 ms 反馈间隔；ISR 缓冲位于内部 RAM。
- 生产固件只复用固定 UAC 1.3.1 的描述符、配置头与 Kconfig。时钟、主/左右声道音量与
  静音、音频接口和 PCM 处理由项目适配层直接持有静态状态，不创建原包的两条音频
  任务或其 PCM 暂存区。USB 控制请求保持 48 kHz 时钟及 -50～0 dB 扬声器音量协议。
  独立诊断仍使用完整原包运行时，其已通过的镜像保持独立验证基线。
- 第二条媒体管线与大缓冲按项目 PSRAM 策略分配。失败的任务退出保留资源供后续清理，
  不会在工作任务或 RTC 回调仍引用时释放。
- 设备音量独立调节喇叭中文译音。电脑输出的主/左右声道音量及静音作用于 LISTEN
  输入，不调节回传电脑的 SPEAK 译音。

## 构建与烧录

```sh
idf.py --preview -B build/mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/mosaico-v1.2/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
```

固件仍为 `build/mosaico-v1.2/mybot_esp32s31.bin`。按住 BOOT 开机进入下载模式，使用实际
下载端口烧录：

```sh
idf.py --preview -B build/mosaico-v1.2 -p /dev/cu.usbmodem144101 flash
```

应用启动后 USB 会以复合设备重新枚举，CDC 接口号发生变化，日志串口名称也可能变化。
重新查看后再启动 monitor：

```sh
ls /dev/cu.usbmodem*
idf.py --preview -B build/mosaico-v1.2 -p 实际运行时串口 monitor
```

## 日志与验收范围

运行时 USB 序列号为 `MOSAICO-` 加这台设备的 MAC，macOS 端口随设备身份枚举。
使用 `ls /dev/cu.usbmodem*` 查询，选取当前应用 CDC；它与 ROM 下载端口不同：

```sh
idf.py --preview -B build/mosaico-v1.2 -p RUNTIME_PORT monitor
```

`ready=1` 只表示 USB 已配置且未挂起。`speaker_open=1` 表示电脑输出流已打开，
`mic_open=1` 表示电脑正在使用 USB 麦克风。两路正常收发的名义速率均为每秒 192,000
字节；`rx_bytes` 和 `mic_bytes` 分别是实际成功接收、发送的字节数。`input_calls` 现在
统计按 USB 时钟提交的麦克风包（HS 约每秒 8,000 次），不再是原 UAC 定时任务的回调。
USB 麦克风在没有
译音时也发送静音，所以 `mic_bytes` 增长本身不代表已经收到英文译音，应录音核对。

`routing=usb_listen_speak` 表示双向会议的 USB 通路已启用。`return_started=1` 表示
回传通路已启动；`return_frames48` 统计提交的译音采样数，`return_peak` 为最近包峰值，
`return_queued48` 为缓冲余量。`return_zero48` 在等待云端译音或双方安静时增长是正常的，
`return_dropped48` 包括电脑未打开麦克风时主动丢弃的译音。

`capture_frames16` 统计电脑音频转换出的 16 kHz 采样，`peak16` 是其最近音频峰值。
`zero_frames16` 和 `dropped_frames48` 分别统计该输入路径的补零与丢弃。稳定持续播放
期间应保持队列有界且不反复补零、丢弃。开始、停流或切换音频设备后可能有短暂静音。

`aosl_tasks` 的任务创建日志会标明 `memory=psram` 或 `internal`；音频、HTTP 和项目
RTC 转发任务应使用 PSRAM，`AgoraRtc` 保留内部栈。若 vendor RTC 初始化失败，固定库
不能安全地在同一次开机内重新初始化，需要重启后再测；云端已创建会话仍按原流程清理。

固定输入/输出设备后持续测试，再分别检查暂停/继续、停止重启和媒体/面对面回归。
`fb_errors` 是反馈失败完成次数，TinyUSB 随后重新提交；需区分切流时的错误和稳定运行
期间持续新增的错误。不要把收发字节数等同于音质或长稳验证。

2026-11-06 用户先确认会议听译能播放中文译音，随后在 PSRAM 任务栈修正版上报告
双向会议测试通过，对应功能验收基线 `e279926`。此后的 review 修复只有 host 测试和
中文构建结果，尚未重新真机验收。量化音质、AEC、重连、长稳、USB 电流与 VCC 软启动
仍需各自验证。先前单向听译镜像保存在历史私有构建目录，验证细节见
[s31-validation.md](s31-validation.md)。独立诊断的 `read_status.py` 不适用于本固件。
