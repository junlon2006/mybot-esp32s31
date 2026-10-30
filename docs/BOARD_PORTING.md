# S31 platform boundaries

[English](BOARD_PORTING.md) · [简体中文](BOARD_PORTING.zh-CN.md)

The only supported profile is `esp32-s31-korvo-1`, selected by the root `MYBOT_BOARD` CMake value. This is a dedicated ESP32-S31 project; earlier ESP32-S3 board profiles and generic drivers are not maintained here.

```text
main/                              startup, device identity, MyBot/network lifecycle
components/mybot_stack/
  mybot_sdk/                       local wrapper + unchanged public SDK snapshot
  aosl/                            unchanged source snapshot
components/mybot_platform/
  boards/esp32-s31-korvo-1/         hardware contract, lifetime, defaults, ADC reference
  ports/aosl/                      source-built S31 AOSL wrapper/configuration
  ports/agora_rtc/                  wrapper + bundled pinned S31 RTSA payload
  src/platform/                    platform registration
  src/services/                    Wi-Fi, HTTPS, NVS, prompts, PCM FIFO, diagnostics
  src/drivers/                     S31 ES8389, keys, touch, RGB/LVGL
  src/internal/                    platform-private interfaces
```

The MyBot boundary is its public `include/mybot/` API. Hardware details stay outside the immutable SDK, AOSL and retained dependency snapshots. Root CMake registers the independent wrapper components; the stack parent directory is a bundle, not an IDF component.

The board profile owns target, defaults, partition, component dependencies and capabilities. `prepare()` initializes hardware before registering the platform; `ensure_network()` returns only after usable connectivity. Provisioning stops MyBot first and returns after reconnection. Input remains board-owned so SET can request provisioning while SDK key callbacks are detached.

Capture and playback expose 16 kHz mono signed-16 PCM **frame counts**. They attach independently to one reference-counted ES8389/TDM bus. Finite prompts drain their playback buffer. Bounded stop/destroy retains ownership on timeout so subsequent initialization/cleanup can recover safely.

LCD controller/buffers are owned by `panels/korvo_s31_rgb_panel.c`. The S31 LVGL adapter coordinates framebuffer flush/VSYNC, touch reads and shared view lifetime. Touch forwards public key events through the S31 input owner; display callbacks perform no network work. Detach prevents late callbacks. All views use the shared LVGL renderer automatically; themes/animations are build settings.

Current build contract: ESP-IDF v6.1 preview `esp32s31`, 16 MB Flash, Octal PSRAM, `partitions/v2/16m.csv`, ES8389, shared LVGL, Newlib and 60 ms RTSA frames. Video is unavailable. Check [the board guide](esp32-s31-korvo-1.en.md), [memory policy](PSRAM_MEMORY.md) and [validation](s31-validation.md) before changing this contract.

Any new lifecycle or callback change needs host failure/timeout/repeated-start tests and separate physical-board results. Source compatibility and compilation do not establish pin routing, acoustic quality or hardware readiness.
