# S31 显示与触摸

[English](PLATFORM_UI.md) · [简体中文](PLATFORM_UI.zh-CN.md)

S31 在 800×480 RGB565、26 MHz 面板上运行共享 LVGL，使用两张 PSRAM framebuffer；GT1151 共用 I2C0。S31 adapter 协调 panel/touch 生命周期，不提供手动 LVGL 开关或旧 renderer。

界面显示联网/配网、配对码、就绪、对话阶段与声纹状态。配网页面显示实际 SoftAP 名，溢出文字独立滚动。中英文、明暗主题与状态动画在构建时选择。

**任意位置按下并释放**：READY 时开始对话，IN_CONVERSATION 时结束。长按在松开时只触发一次；首版不区分拖动，正常松开仍算一次手势。初始化/读错误恢复时手指已按下，必须先松开再按；配网、配对、启动、失败和 SDK detached 阶段忽略触摸，手势不跨 detach/reattach。PLAY/SET/VOL 实体键保持原行为。

显示输入只向 S31 key owner 提交小型待处理事件，由后者在 LVGL 锁外调用 SDK 公开按键回调，复用 PLAY 对话逻辑。触摸/渲染回调不阻塞等待 HTTP/RTC。可搜索 `korvo_s31_keys: event=touch action=conversation_start` 或 `conversation_stop` 确认事件。

用户已确认真机屏幕正常显示，尚不能据此确认触点、单手势单事件或新增开停对话行为；这些待上板验收。host 测试覆盖回调顺序、detach、初始化/清理失败及超时，结果见[验证记录](s31-validation.md)。

共享 Source Han Sans/Noto 素材许可见[第三方声明](../THIRD_PARTY_NOTICES.md)。明亮/静态设置使用同一共享渲染路径，不新增硬件板型。视频保持关闭。
