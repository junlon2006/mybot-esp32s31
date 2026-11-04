# S31 独立听译

固定链路：板载 MIC → 现有云端 AEC/RTC → Bot 翻译 → RTC 译音 → 板载喇叭。输入与播放持续并行。SDK 只接入 `v2vt/listen`，不实现字幕或其他业务模式。

## 配置与操作

- 开发服务：`https://mybot.sh3t.agoralab.co/api`。设备须在该部署绑定已开启听方向译音的 V2VT Bot，旧服务的设备凭证不会跨服务复用。
- 默认中文 `zh-CN` → 英语 `en-US`。就绪时点击“输入语言”或“目标语言”循环选择，排除同语种；选择写入 NVS，已有保存的选择优先于固件默认值。默认值也可通过 menuconfig 的 `MYBOT_SOURCE_LANGUAGE` / `MYBOT_TARGET_LANGUAGE` 修改。
- 点击“开始翻译”或短按 PLAY 开始；再次点击停止。连接中可以取消。活动会话中锁定语言，停止确认完成后再更换。
- 首页进入场景选择后，媒体翻译接真实音频；面对面与会议互译目前提供设置预览。字幕、全文及历史为明确标记的固定演示，页面与控制说明见[翻译界面](PLATFORM_UI.zh-CN.md)。
- 实时页可暂停/继续本地音频，RTC 父会话保持，暂停期间继续上行静音帧。恢复会清除旧 PCM，音量保持；暂停不是结束云端会话。
- 停止失败时关闭本地音频并继续清理，不能创建替代会话；断网重连/重启后先清理旧父会话。若显示启动结果未知，应在服务端核实后处理，不能通过重复启动创建会话。
- 每场翻译结束时释放 RTC 服务和回调队列，下一场使用当前 Bot 返回的 App ID 重新初始化，支持结束后换绑其他项目。`RTC_NOT_READY` 表示本地 RTC 尚未完成清理，新云会话不会发起；可以重试，若 RTSA 全局释放失败则需重启设备。
- Bot 仅字幕或返回普通对话/双向模式时，固件显示错误并清理父会话。必须在 Bot 中启用译音。

## 构建和烧录

开发只构建中文，使用独立的生成配置，避免旧 sdkconfig 保留旧服务器：

```sh
. /Users/shangjinlong/esp/esp-idf/export.sh
idf.py --preview -B build/listen-zh \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/listen-zh/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
idf.py --preview -B build/listen-zh -p PORT flash monitor
```

保留现有 G.722、16 kHz mono S16、60 ms RTSA 配置和 cloud AEC。AI QoS 默认关闭（`CONFIG_MYBOT_AI_QOS=n`），可在 menuconfig 中显式开启；关闭时启动请求发送 `ai_qos=false` 且不附带快发倍率。已有 sdkconfig 中的显式配置优先于默认值。云 AEC 上行是 MIC/播放参考交错，RTSA 的 PCM channel 数仍为 1，不能改成普通立体声。先检查最终 sdkconfig 的服务器、语种和 AI QoS 设置，再烧录。

## 验收

主机测试验证路由/父 ID/清理/回调所有权和界面；编译验证 RISC-V 链接与配置。实机需要验证真实服务授权、目标语种译音、指定 UID 订阅、播放同时讲话时云 AEC、音量变化、连接中取消、断网与反复启停。既有普通对话音频实测不能替代这些结果。

## 启动排查与恢复

所有 HTTPS 请求和响应正文均通过 `event=http_body` 打印，包括配对、绑定查询、翻译启动和停止。每个 request/response body 在一条 `phase=data data=...` 日志中完整输出，空正文同样输出 `data=`；通过连接 `id` 和 `direction` 区分。`phase=begin/end` 保留长度和完整性信息，begin 还提供方法、脱敏路径、HTTP 状态、传输错误及实际/捕获字节数。含 NUL 的二进制正文一次输出完整 hex，临时缓冲分配失败会明确标记输出不可用。无响应、部分响应、内存不足和捕获超限都会标记原因。日志在连接关闭后输出，请求日志时间不是实际发送时间。请求头不打印，正文可能含服务凭证，分享前需脱敏。

- `TIME_NOT_SYNCED`：设备 UTC 尚未就绪，启动请求没有发送，不会创建云会话。固件依次尝试 `ntp.aliyun.com`、`time.cloudflare.com`、`pool.ntp.org`；已通过现有 TLS 验证的配对/绑定等 2xx 响应也可用合法的 GMT `Date` 响应头引导初始时间，日志为 `event=time_sync source=https_date result=ok`。这一途径只设置尚未初始化的时钟，正常时钟继续交给 SNTP。收到时间同步成功日志后再开始，路由有效期仍必须通过校验；过期与时间格式错误分别报告。
- 翻译启动使用连接、发送和接收共享的 30 秒总预算，其余请求保持 5 秒。总预算不随分片进展重置，也不自动重发启动。日志 `event=translation action=start phase=result` 会输出服务结果、错误码、父 ID 是否已知及清理标记，便于区分云端启动成功后本地拒绝和 HTTP 没有返回。
- `START_RESULT_UNKNOWN` / “启动结果未知”：请求可能已发送，但没有拿到父会话 ID。此状态写入 NVS，普通 `idf.py ... flash` 和重启都会保留，不能直接重复创建会话。
- 仅在控制台已确认该设备全部会话结束后，可从 `0x0` 烧录本次合并镜像 `releases/esp32-s31-listen-zh-CN.bin` 恢复。该镜像的 `0x9000..0xcfff` 为擦除值，会清除 NVS 中的未知会话记录、Wi-Fi、绑定、语言和音量设置，需要重新配网、配对。烧录前请核对该镜像及相邻 `.sha256` / `.flash.txt`；正常升级继续使用上面的分段 `idf.py flash`。
