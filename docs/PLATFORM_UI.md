# S31 translation UI

The native 800×480 LVGL interface follows `device-translation-flow.zh-CN.html`: pastel blue surfaces, a circular home action, three scene cards, and sound/display controls. Existing RGB/GT1151 drivers, shared I2C ownership and PSRAM framebuffers are retained.

Media translation uses the real `v2vt/listen` microphone-to-cloud-to-speaker path, initially Chinese to English. Face-to-face and meeting scenes provide settings previews with disabled start actions. Captions, full text and history use explicitly labelled fixed demo content. Wi-Fi, system time, session state and volume use real values; battery is unavailable and brightness is a local demonstration.

The home/scene/settings/language/live/menu/confirmation/completion/error pages follow SDK lifecycle snapshots. A queued command never fabricates a successful connection. Active language changes require confirmed session teardown. Cleanup and unknown outcomes continue to block new sessions; late start success after cancellation leaves a usable stop/retry action. Definite start errors and failed local media recovery remain visible.

`mybot/mybot_ui.h` exposes the shared 15-language list and bounded, nonblocking commands copied by value. Language selection submits directly through this public interface to check admission; volume, pause and key events retain the existing input worker. The SDK control owner validates and applies all commands. An accepted language pair locks both language selectors and start until that pair is read back or an actual failure occurs; unrelated volume snapshots and the local retry timer cannot unlock it. A rejected submission can be retried immediately. A released volume drag commits once, and confirmed volume/pause/language values come from LCD snapshots. Physical volume and PLAY keys remain available.

Local pause preserves capture and the RTC parent session, sends silent MIC/reference frames at the existing 60 ms cadence, and stops playback independently to clear driver-owned PCM. Resume fences workers, clears stale data and restarts playback before confirming success. Hardware volume is preserved; pause is not a cloud session stop.

The view uses generated 20/28/40 px Noto Sans SC subsets with retained OFL notices and local graphics, with no browser runtime or online asset dependency. Host UI, input, lifecycle, mailbox, audio and pause tests check behavior and ownership; actual panel/touch/audio behavior requires device acceptance. See [translation and flashing](LISTEN_TRANSLATION.zh-CN.md).
