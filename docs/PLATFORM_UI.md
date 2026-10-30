# S31 display and touch

[English](PLATFORM_UI.md) · [简体中文](PLATFORM_UI.zh-CN.md)

The S31 port uses the shared LVGL view on an 800×480 RGB565 panel at 26 MHz, with two PSRAM framebuffers and GT1151 touch on the shared I2C0 bus. Panel and touch lifecycle are coordinated by the S31 adapter; there is no manual LVGL-enable switch or legacy renderer.

The view displays network/provisioning state, pairing codes, readiness, conversation phases and voiceprint indicators. Provisioning displays the actual SoftAP name. Overflowing provisioning text scrolls independently of conversation animations. Chinese/English, dark/light theme and state animation settings are selected at build time.

Tap anywhere and release to start from READY; during a conversation, tap and release to stop. A held touch produces one event on release. Dragging is not distinguished from a tap in this first implementation. A finger already down at initialization/read recovery must be released and pressed again. Touch while provisioning, pairing, starting, failed or detached is ignored; gestures do not carry across SDK detach/reattach. PLAY/SET/VOL physical controls remain unchanged.

The display input reports a small pending event to the S31 key owner, which invokes the SDK's public key callback outside the LVGL lock. It follows the existing PLAY behavior and does not block touch/render callbacks on HTTP or RTC. Search `korvo_s31_keys: event=touch action=conversation_start` or `conversation_stop` to confirm dispatch.

The user has confirmed normal screen output on the physical board. This does not confirm touch detection, one-event-per-gesture behavior or the new start/stop interaction; those remain pending hardware acceptance. Host tests exercise callback ordering, detach, initialization/cleanup failure and timeout. See [validation](s31-validation.md).

Shared assets retain Source Han Sans/Noto licenses; see [third-party notices](../THIRD_PARTY_NOTICES.md). Optional theme/static settings use the same shared rendering path without adding board variants. Video remains disabled.
