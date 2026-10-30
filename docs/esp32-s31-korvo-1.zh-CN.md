# ESP32-S31-Korvo-1 V1.1

[English](esp32-s31-korvo-1.en.md) · [简体中文](esp32-s31-korvo-1.zh-CN.md)

Board ID 为 `esp32-s31-korvo-1`，基于 ESP-IDF v6.1 的 preview S31 target。包含 Wi-Fi/网页配网、HTTPS/配对、RTC/RTM、ES8389 双向语音、提示音、ADC 按键和 RGB/GT1151 界面。视频不在当前移植范围。

## 编译和烧录

安装 ffmpeg。固定版本 S31 RTSA 头文件/库随工程提供，直接编译：

```sh
. /path/to/esp-idf/export.sh
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build
idf.py --preview -B build/korvo-s31 -p PORT flash monitor
```

开发直接运行 `idf.py`。板载 USB-UART 用于烧录和 UART0 日志（TX58/RX59），独立供电 USB 口只供电。英文构建使用独立目录/sdkconfig，并在 defaults 末尾追加 `;ci/en-us.defaults`。修改 defaults 后应重新配置并检查 sdkconfig；旧配置不会自动被默认值覆盖。

提示音由 ffmpeg 在构建目录从 Ogg 转为 16 kHz mono S16LE，固件无需 S3 Opus 解码库。交付镜像与校验文件见[交付说明](RELEASING.zh-CN.md)。`idf.py flash` 日常烧录保留 NVS；合并镜像写入 **0x0**，覆盖 NVS 所在间隙，之后需重新配网。

## 操作

| 操作 | 行为 |
| --- | --- |
| 轻点屏幕 | 开启对话；对话中再次轻点结束 |
| PLAY 短按 | 同样开启/结束对话 |
| SET 短按 | 请求重新配对 |
| SET 长按 3 秒 | 停止 MyBot 后进入配网 |
| VOL+/VOL- | 调整音量并保存 |
| 首次未配置 Wi-Fi | 自动进入网页配网并显示入口/播放提示 |

等待 Wi-Fi、配对完成并进入就绪界面后开始对话。触摸通过与 PLAY 相同的公开按键事件进入业务流程，显示/触摸回调不执行 HTTPS 或 RTC 阻塞操作。未就绪时忽略触摸。长按松开只触发一次；首版不区分拖动，正常释放仍视为一次手势。初始化或读错误恢复时已按住，需先松开再重新按。触摸日志为 `event=touch action=conversation_start` / `conversation_stop`。

用户已确认真机双向音频和屏幕正常显示；新增触摸开启/结束逻辑仍待上板测试。轻点开会话、再次轻点结束，确认一次手势只触发一次，再确认 PLAY 和 SET 仍正常。详见[验证记录](s31-validation.md)。

## 硬件与 TDM

| 项目 | 当前配置 |
| --- | --- |
| 模组/存储 | ESP32-S31-WROOM-3，16 MB Flash、16 MB PSRAM |
| Codec | ES8389，I2C0 SDA0/SCL1 |
| 音频总线 | 16 kHz，4×16-bit TDM，64 BCLK/frame；BCLK3、WS4、TX5、RX6 |
| 时钟/功放 | Codec 使用 BCLK，CPU 不输出 MCLK；PA GPIO7 |
| SDK 音频 | mono signed 16-bit PCM、16 kHz、60 ms 帧 |
| LCD | 800×480 RGB565、26 MHz、两张 PSRAM framebuffer |
| 触摸 | GT1151，共用 I2C0；reset/IRQ 为 NC |
| 显示控制 | 背光和显示使能为 NC，不配置假定 GPIO |
| ADC 按键 | GPIO42 电阻梯，使用官方软件校准源 |
| 分区 | `partitions/v2/16m.csv`，两个 `0x3f0000` OTA slot |

按官方文档要求使用 TDM，未照搬固定 factory BSP 的标准 stereo I2S 初始化。ES8389 `use_mclk=false`、`no_dac_ref=false`，适配器检查 STDM 寄存器，播放映射时隙、采集选定麦克风时隙。名义 BCLK 为 1.024 MHz、WS 为 16 kHz，未声称经仪器测量。

基础音频互通通过不代表双麦/参考时隙归属、量化音质或长稳已验收；本地 AEC/波束成形/唤醒、视频、SD/USB 应用和低功耗均未移植。本轮仅增加触摸交互、CI/交付和工程整理，其余项目保持待定。

## 开启对话正文日志

`POST .../conversations/start` 的请求和响应完整打印，含 HTTP 错误响应。筛选 `event=http_body`，用连接 `id` 和 `direction=request|response` 归组。`begin/end` 给出字节数和完整性，`offset` 表示顺序；中断标记 incomplete。普通 JSON 按有界 UTF-8 分段，含 NUL 的正文用 hex 保留全部字节。日志可能含认证和设备信息，分享前脱敏。

## 参考

硬件事实和 ADC 校准源来自 [Espressif 官方文档](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html)及 [esp-dev-kits 固定 BSP](https://github.com/espressif/esp-dev-kits/tree/bd4d4ac18657e73583af3d7c0b0a23c3cddf3735/examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo)。只引入所需硬件参数与授权 ADC 源，不依赖 factory_demo 的应用或素材。具体来源和条款见根目录来源/许可文件。
