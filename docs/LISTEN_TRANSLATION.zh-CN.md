# S31 媒体与面对面翻译

固定链路：板载 MIC → 现有云端 AEC/RTC → Bot 翻译 → RTC 译音 → 板载喇叭。输入与播放持续并行，同一会话的 RTM 原文/译文进入实时字幕、全文和本次历史。媒体翻译使用 `v2vt/listen`；面对面使用一个 `v2vt/listen_speak` 父会话和两个 RTC 发布身份。会议互译尚未接入业务。

## 配置与操作

- 开发服务：`https://mybot.sh3t.agoralab.co/api`。设备须在该部署绑定 V2VT Bot。面对面还要求部署开放双向模式，且两个 Agent 的字幕均发送到共同 RTM 频道。旧服务的设备凭证不会跨服务复用。
- 默认中文 `zh-CN` → 英语 `en-US`。就绪时点击“输入语言”或“目标语言”，在列表中直接选择，排除同语种；提交期间锁定语言选择，实际回读后解锁。选择写入 NVS，已有保存的选择优先于固件默认值。默认值也可通过 menuconfig 的 `MYBOT_SOURCE_LANGUAGE` / `MYBOT_TARGET_LANGUAGE` 修改。
- 点击“开始翻译”或短按 PLAY 开始；通过结束控件或再次短按 PLAY 停止。连接中可以取消。活动会话中锁定模式与语言，停止确认完成后再更换；已提交开始请求时也不能改变模式或语言。
- 首页进入场景选择后，媒体翻译和面对面接真实音频与 RTM 字幕；会议互译提供设置预览。点击实时字幕查看可滚动全文，更多菜单及完成页可查看本次历史；最多保留最近 64 句，每侧最多 1,023 个 UTF-8 字节，重启后不保留。首期字形覆盖面向中文与英文，详情见[翻译界面](PLATFORM_UI.zh-CN.md)。
- 实时页可暂停/继续本地音频，RTC 父会话保持，暂停期间继续上行静音帧。恢复会清除旧 PCM，音量保持；暂停不是结束云端会话。
- 停止失败时关闭本地音频并继续清理，不能创建替代会话；断网重连/重启后先清理旧父会话。若显示启动结果未知，应在服务端核实后处理，不能通过重复启动创建会话。
- 每场翻译结束时释放 RTC 服务和回调队列，下一场使用当前 Bot 返回的 App ID 重新初始化，支持结束后换绑其他项目。`RTC_NOT_READY` 表示本地 RTC 尚未完成清理，新云会话不会发起；可以重试，若 RTSA 全局释放失败则需重启设备。
- listen 路由允许仅字幕，此时 `tts_enabled=false` 且 `audio_publisher_uid=null`；speak 路由必须有译音。普通对话、错误模式、缺失路由、重复身份或不一致的频道/项目/有效期都会拒绝接入并清理父会话。

## 面对面行为

面对面设置中的第一个语种是“我的语言”，第二个是“对方语言”。我的语言为中文、对方语言为英文时，请求 `local_language=zh-CN`、`remote_language=en-US`：listen 为英文 → 中文，speak 为中文 → 英文。媒体设置的两个语种仍表示输入与目标，不能用媒体方向解释面对面的本方与对方。

启动仅创建一个父会话。固件使用 listen 的 UID/token 登录一次 RTM、订阅共同频道一次，然后使用两路各自的 UID/token 创建两个 RTC 连接。每路只订阅返回的译音发布者；两路入会及所需译音订阅都完成后才开放发言。接入或重连后超过 15 秒仍未就绪、任一必要 Agent 离线或凭证将过期，均结束整场父会话。当前固定 RTSA 包的 RTM 只支持一个登录；本方案不调用第二次 RTM 登录，也不修改该库。

面对面连接后默认不选择发言方向。点击语言按钮选择当前讲话人的语言，只把板载 MIC 送入对应路由；另一方向每 60 ms 发送零 MIC，云 AEC 开启时两路保留相同的实际播放参考。方向切换只改本地音频路由，不创建新云会话。切换时暂停并清理旧 PCM，成功后回读新方向；本地恢复失败时保留可查询的暂停状态。板载喇叭仅播放当前选择方向的译音，非当前方向的迟到译音会丢弃；两路字幕持续按发布者归属到正确方向，不随按钮切换重新分类。

双向字幕仍共用最近 64 句缓存，配对键为 `(direction, stream_id, turn_id)`，全文和历史携带方向信息。麦克风没有自动识别讲话人，使用者需点击正确的语言按钮。静音上行不保证延长云端 Agent 的闲置时限，单侧长时间闲置仍需在实际部署验收。

## 构建和烧录

开发只构建中文，所有本地编译固定使用 `build/korvo-s31`，生成配置固定为 `build/korvo-s31/sdkconfig`：

```sh
. /Users/shangjinlong/esp/esp-idf/export.sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
idf.py --preview -B build/korvo-s31 -p PORT flash monitor
```

保留现有 G.722、16 kHz mono S16、60 ms RTSA 配置和 cloud AEC。AI QoS 默认关闭（`CONFIG_MYBOT_AI_QOS=n`），可在 menuconfig 中显式开启；关闭时启动请求发送 `ai_qos=false` 且不附带快发倍率。已有 sdkconfig 中的显式配置优先于默认值。云 AEC 上行是 MIC/播放参考交错，RTSA 的 PCM channel 数仍为 1，不能改成普通立体声。先检查最终 sdkconfig 的服务器、语种和 AI QoS 设置，再烧录。

## 验收

主机测试验证路由/父 ID/清理/回调所有权、字幕归并和界面；编译验证 RISC-V 链接与配置。实机需要分别验证媒体与面对面的真实服务授权、两个方向译音、指定 UID 订阅、单 RTM 接收双 Agent 字幕、方向快速切换与闲置一路、RTM 原文/译文消息与流式修订、长字幕/历史滚动、播放同时讲话时云 AEC、音量变化、连接中取消、断网与反复启停。既有普通对话音频实测不能替代这些结果。

## 启动排查与恢复

所有 HTTPS 请求和响应正文均通过 `event=http_body` 打印，包括配对、绑定查询、翻译启动和停止。每个 request/response body 在一条 `phase=data data=...` 日志中完整输出，空正文同样输出 `data=`；通过连接 `id` 和 `direction` 区分。`phase=begin/end` 保留长度和完整性信息，begin 还提供方法、脱敏路径、HTTP 状态、传输错误及实际/捕获字节数。含 NUL 的二进制正文一次输出完整 hex，临时缓冲分配失败会明确标记输出不可用。无响应、部分响应、内存不足和捕获超限都会标记原因。日志在连接关闭后输出，请求日志时间不是实际发送时间。请求头不打印，正文可能含服务凭证，分享前需脱敏。

- `TIME_NOT_SYNCED`：设备 UTC 尚未就绪，启动请求没有发送，不会创建云会话。固件依次尝试 `ntp.aliyun.com`、`time.cloudflare.com`、`pool.ntp.org`；已通过现有 TLS 验证的配对/绑定等 2xx 响应也可用合法的 GMT `Date` 响应头引导初始时间，日志为 `event=time_sync source=https_date result=ok`。这一途径只设置尚未初始化的时钟，正常时钟继续交给 SNTP。收到时间同步成功日志后再开始，路由有效期仍必须通过校验；过期与时间格式错误分别报告。
- 翻译启动使用连接、发送和接收共享的 30 秒总预算，其余请求保持 5 秒。总预算不随分片进展重置，也不自动重发启动。日志 `event=translation action=start phase=result` 会输出服务结果、错误码、父 ID 是否已知及清理标记，便于区分云端启动成功后本地拒绝和 HTTP 没有返回。
- `START_RESULT_UNKNOWN` / “启动结果未知”：请求可能已发送，但没有拿到父会话 ID。此状态写入 NVS，普通 `idf.py ... flash` 和重启都会保留，不能直接重复创建会话。
- 仅在控制台已确认该设备全部会话结束后，可从 `0x0` 烧录本次合并镜像 `build/korvo-s31/mybot_esp32s31-merged.bin` 恢复。该镜像的 `0x9000..0xcfff` 为擦除值，会清除 NVS 中的未知会话记录、Wi-Fi、绑定、语言和音量设置，需要重新配网、配对。烧录前请核对该镜像及相邻 `.sha256` / `.flash.txt`；正常升级继续使用上面的分段 `idf.py flash`。
