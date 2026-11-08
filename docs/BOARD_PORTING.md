# S31 platform boundaries

[English](BOARD_PORTING.md) · [简体中文](BOARD_PORTING.zh-CN.md)

The default profile is `esp-mosaico-v1.2`, with the earlier `esp32-s31-korvo-1` retained, selected by the root `MYBOT_BOARD` CMake value. This is a dedicated ESP32-S31 project; earlier ESP32-S3 board profiles and generic drivers are not maintained here.

```text
main/                              startup, device identity, MyBot/network lifecycle
components/mybot_stack/
  mybot_sdk/                       component wrapper + maintained V2VT translation SDK
  aosl/                            upstream sources + S31 component wrapper
  agora_rtc/                       S31 RTSA header, library and component wrapper
components/mybot_platform/
  boards/esp-mosaico-v1.2/          current hardware, lifetime and defaults
  boards/esp32-s31-korvo-1/         earlier hardware and ADC reference
  src/platform/                    platform registration
  src/services/                    Wi-Fi, HTTPS, NVS, prompts, PCM FIFO, diagnostics
  src/drivers/                     ES8311/ES8389, keys, touch, QSPI/RGB/LVGL
  src/internal/                    platform-private interfaces
```

The platform uses MyBot through its public `include/mybot/` API. The SDK is a project-maintained V2VT media/face-to-face/meeting fork; hardware details stay in the platform component. AOSL outside its wrapper/S31 adapter and retained dependency snapshots remain immutable. Root CMake registers the three independent runtime components under `components/mybot_stack`; the stack parent directory is a bundle, not an IDF component.

The board profile owns target, defaults, partition, component dependencies and capabilities. `prepare()` initializes hardware before registering the platform; `ensure_network()` returns only after usable connectivity. Provisioning stops MyBot first and returns after reconnection. Input remains board-owned so maintenance gestures can request provisioning while SDK key callbacks are detached.

Capture and playback expose 16 kHz mono signed-16 PCM **frame counts**. They attach independently to one reference-counted codec bus: ES8311/I2S stereo/MCLK256 on Mosaico or ES8389/TDM4 on Korvo. Finite prompts drain their playback buffer. Bounded stop/destroy retains ownership on timeout so subsequent initialization/cleanup can recover safely.

Board-specific panel files own controller/DMA buffers. Mosaico uses QSPI partial transfers with separate draw/staging buffers; Korvo uses RGB framebuffer/VSYNC. Both LVGL adapters coordinate touch reads and shared view lifetime. Language, mode and direction selections submit copied commands directly through the nonblocking `mybot_ui_submit()` API and check admission. Volume, pause and key events use the S31 input worker. Display callbacks perform no network work. Detach prevents late callbacks. Display boards use the shared native LVGL translation view automatically; scene behavior is described in [the UI guide](PLATFORM_UI.md).

Current build contract: ESP-IDF v6.1 preview `esp32s31`, 16 MB Flash, Octal PSRAM, `partitions/v2/16m.csv`, board-specific codec, shared LVGL, Newlib and 60 ms RTSA frames. Video is unavailable. Check [the board guide](esp-mosaico-v1.2.zh-CN.md), [memory policy](PSRAM_MEMORY.md) and [validation](s31-validation.md) before changing this contract.

Any new lifecycle or callback change needs host failure/timeout/repeated-start tests and separate physical-board results. Source compatibility and compilation do not establish pin routing, acoustic quality or hardware readiness.

Mosaico uses ES8311 standard I2S stereo/MCLK256 and a native 480×480 CO5300 QSPI/CST9220 view. Shared audio lifecycle keeps bounded I/O and independent capture/playback. Touch controls translation; the AI key is maintenance-only: short presses do nothing, three seconds provisions Wi-Fi and ten seconds requests factory reset. Type-C provides composite UAC2 audio and CDC logs. Meeting LISTEN uses PC audio and plays on the codec; SPEAK uses physical MIC and returns translated speech to USB IN. See the [Mosaico board guide](esp-mosaico-v1.2.zh-CN.md).
