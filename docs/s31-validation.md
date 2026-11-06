# S31 validation record

The current development target is ESP-Mosaico CoreBoard V1.2. The latest checks are under [RTC initialization and PSRAM task stacks](#2026-11-06-rtc-initialization-and-psram-task-stacks). Earlier sections describe the revision tested at that time. Use the manifests and checksums beside delivered images for their current binary identity.

This records local build/host validation and separately attributed hardware feedback. Basic bidirectional audio passed the user's real-board interoperability test. The user subsequently confirmed normal screen display. Detailed TDM channel/reference mapping, quantitative audio quality, the new touch behavior, power sequencing and long-duration stability are not established by those reports.

## User-reported hardware audio test — 2026-10-30

The user reports that ESP32-S31 audio interoperability is working normally on the physical board. This is user-performed hardware validation, not a host-test inference.

The supplied serial log corroborates:

- ES8389 initialized with the configured 16 kHz, TDM4 × 16-bit controller, 64 BCLK/frame, Philips framing and capture slot 0. Register readbacks include ADC serial-TDM enabled. Clock rates are configured values; no logic-analyzer measurement was supplied.
- PLAY key events were detected for conversation start and stop.
- Conversation start returned HTTP 201. Request and response body traces were complete: 126 and 951 bytes respectively. Raw bodies, tokens and device identifiers are intentionally not copied into this record.
- RTC joined successfully in 182 ms, entered CONNECTED, received an audio codec update, and later left/destroyed the connection normally.
- Conversation stop returned HTTP 200. Saved volume was restored from NVS (value 40).

Scope: basic microphone/speaker interoperability is confirmed by the user's report, with one conversation start/stop sequence evidenced in the log. Repeated-cycle endurance, channel/reference ordering and full-board readiness remain separate validation tasks.

## Historical initial-build evidence (before the touch/delivery update)

- ESP-IDF v6.1; GCC 15.2.0 / esp-15.2.0_20251204; Newlib; esp32s31 preview target.
- Both Chinese and English `idf.py --preview -B <variant> build size` completed successfully after the final audio ownership fix.
- Both generated sdkconfigs have PSRAM, ES8389, shared LVGL and 60 ms configuration enabled; video is disabled.
- Final component lists contain no esp_video, esp_cam_sensor, esp_new_jpeg or esp_audio_codec. No S3 binary is linked.
- Both link maps select only RISC-V RTSA archive objects; AOSL memory/thread/OS-version symbols come from the locally built S31 AOSL component. ISA/ABI link acceptance is not a substitute for silicon runtime testing.
- MyBot snapshot (64 files), AOSL snapshot (250 files) and the pinned ADC reference source remain byte-identical to their inputs. The complete official esp_codec_dev 1.5.9 package matched its 92 registry file checksums.
- Standard esptool merge using each build's generated flash_args produced the images below. Each merged image was checked to contain the exact built application at 0x20000.

| Language | App bytes | Merged bytes | Merged SHA-256 |
| --- | --- | --- | --- |
| zh-CN | 2567504 | 2698576 | `16117ad494178efd71606ac8ef71c93cd5fb8ea47d0c9e30835261730bfaa6b1` |
| en-US | 2648160 | 2779232 | `886f693238e1f1c0c26374a1e23f34d6b32ca9bfba32ba75608c796afcc47309` |

The 0x3f0000 application slots have more than 36% space remaining. These are historical initial-image hashes, not checksums of the new touch/delivery revision. Current per-language manifests are generated alongside new images. Firmware outputs remain ignored and have not been published.

## Historical initial host and source checks

- ES8389 real-driver fault injection plus UBSan: allocation/initialization failures, eleven start failures, PCM slot conversion, capture/playback interleaving, callback/stop timeout, partial cleanup, persisted volume and deferred ownership recovery passed.
- RGB/GT1151: 23 lifecycle/failure scenarios passed normally and with UBSan.
- ADC input and board: 9 key + 5 board scenarios passed normally and with UBSan, including callback ordering and provisioning while the SDK is detached.
- Shared PCM buffer/PSRAM lifecycle regressions passed. Legacy camera host regressions also passed at that time; those tests/drivers have now been removed from the dedicated no-video S31 project.
- Builder: 40 tests on IDF Python 3.10, 35 passed and 5 Python 3.11+ filesystem-cleanup cases accurately skipped. Old-Python cleanup fails closed. Dual-language dry-run produced preview commands without video or filesystem cleanup.
- Platform/main formatting, SPDX and new source/test whitespace checks passed; third-party ADC snapshot/vendor headers are excluded from reformatting. Workflow YAML and its input-test command were parsed/checked. Remote CI was not triggered.

## Initial hardware checklist (remaining items deferred)

1. Flash via the onboard UART bridge, confirm 16 MB Flash/PSRAM startup and ADC calibration.
2. Check `event=audio_bus` register readbacks. Nominal BCLK is 1.024 MHz and WS is 16 kHz; verify the selected microphone slot and DAC-reference ordering on hardware.
3. Provision, pair, listen to prompts/downlink, then check microphone uplink. Test PLAY/SET/VOL keys and RGB/GT1151.
4. Repeat conversation start/stop, provisioning/reconnect and saved volume. Capture internal heap/PSRAM, task stack and per-core runtime metrics during longer tests; these have not been measured on this board yet.

The driver takes ownership at void destroy(). If teardown times out, it retains the context and retries at the next init/destroy; callers must not retry a destroyed pointer.

## Conversation-start body logging update — 2026-10-30

- The platform HTTPS transport now logs request/response bodies for conversation start after the connection closes. Partial writes are counted once, HTTP errors are included and chunked framing is removed. Each record carries a connection ID, direction and byte offset; incomplete data is marked explicitly. JSON is emitted in bounded UTF-8 chunks; NUL-containing bodies use full hex output.
- 20 synthetic scenarios with UBSan passed, covering fragmentation, short writes, large responses, chunked extensions/trailers and partial chunks, HTTP errors, EOF/timeouts, concurrent trace objects, non-target requests, allocation failure and NUL bytes. Formatting, SPDX, whitespace and workflow registration checks passed.
- Both target-project language builds passed. Both merged firmware images and the manifest above were regenerated from those builds and their application regions were verified. Video remains disabled and the final maps still select only RISC-V RTSA objects.
- The MyBot/AOSL source snapshots remain unchanged. The initial implementation handoff preceded hardware testing; subsequent user hardware results are recorded separately above.

## Screen and touch/delivery update — 2026-10-30

- The user reports that the physical screen displays normally. This is user-performed display validation; it does not establish GT1151 touch or conversation toggling.
- That revision added press/release conversation start/stop through the public SDK key callback, retained physical PLAY/SET/VOL, and removed unused S3 project material while preserving dependency snapshots. It included bundled pinned RTSA payloads, push/PR language builds and per-language checked delivery metadata. Its screen-wide toggle was replaced by the native scene controls recorded below.
- New touch real-device acceptance and a recorded remote CI result remain pending. The initial-build results and SHA-256 table above must not be attributed to this revision.
- Other audio tuning, input/network regression, endurance/resource measurement and extension work are deferred at the user's request. Video remains disabled.

## Touch control, CI delivery and S31-only baseline — 2026-10-30

- The user has confirmed normal screen display as well as the previously recorded bidirectional audio test. New screen-touch start/stop behavior has not yet been hardware-accepted.
- Touch input now submits one state-specific command on release through a nonblocking mailbox to the existing key worker. Waiting/long press does not repeat; read errors cancel a gesture; unready and detached SDK generations cannot replay stale touches. Physical keys remain available.
- After removing unrelated S3 components/profiles/tests, local input/board (17 cases), display (23 cases), shared PCM buffer, audio lifecycle and HTTPS trace (20 synthetic scenarios) passed. Input/display also passed UBSan. Audio source and the MyBot/AOSL/codec/LVGL/port snapshots remained byte-identical.
- Builder and delivery tests: 52 cases under local Python 3.10, 47 passed and 5 Python 3.11+ filesystem-cleanup cases skipped. The CI quality job selects Python 3.12 to exercise those cases. Project formatting, SPDX, whitespace and workflow YAML/Bash checks passed.
- Direct IDF v6.1 preview builds and size checks passed for both languages in the cleaned target project. The actual absolute-path merge-bin and shared checker flow passed for both; each of four flash segments, target/config, bundled RTSA fingerprint, RISC-V ELF and local AOSL map ownership was verified.
- The approved original RTSA library/header/config/package identity are now versioned inputs. No CI URL, secret or import is required. Push/PR/manual workflows build both languages and upload the four delivery files. Remote Actions results are not recorded in this checkout.
- Delivery files are `.bin`, `.manifest.json`, `.sha256` and `.flash.txt` per language. Flash instructions derive reset, no-stub and flash settings from that build. The merged image starts at 0x0.

The following hashes describe the local verification images. Final delivery checksums are in each release `.sha256` / `.manifest.json`.

| Language | Merged bytes | SHA-256 |
| --- | --- | --- |
| zh-CN | 2699216 | `6bdbbb77f662f4d8bf190b27d46c875f0e2742dfaf97f016c47a89c6b513756e` |
| en-US | 2779872 | `cf15cadf9b172862b2ecd5280d76b9b94a28c80768f73dea90bb6c7998a18fbf` |

Other requested-deferred features, audio tuning, endurance testing and video were not added in this update.

### Clean repository-input verification

The exact staged Git blobs were exported to a new source directory with no local build outputs, managed-component directory or ignored SDK archive. All tracked MyBot/AOSL/codec/LVGL/port source bytes (including original line endings) matched the working inputs. The optional, unused Xtensa-only ZL38063 archive was absent. A fresh Chinese IDF v6.1 preview build, size report, absolute-path merge and shared delivery checker all passed from this export. The final target-project Chinese and English builds also passed after adding the S31 guard against that unsupported codec. No remote GitHub Actions run or new touch hardware test is claimed.

The pre-layout source baseline was developed on `dev` and merged into `main` at
`9a796a84d82ff6ab4cd4206571d6f46c95393b6f`. Delivery manifests record the exact revision
and dirty state used for each image. No successful remote GitHub Actions result is recorded here.

## ESP32-aligned runtime layout — 2026-10-30

- Moved the runtime components to `components/mybot_stack/{mybot_sdk,aosl,agora_rtc}`,
  matching the ESP32 project. `components/mybot_platform/ports` no longer exists.
- Kept MyBot and AOSL source snapshots unchanged. AOSL is built by its component-root CMake
  wrapper with S31 configuration and an OS-version adapter under `aosl/s31_port/`.
- Moved the approved RTSA header, library and package metadata into `agora_rtc/include`,
  `agora_rtc/lib/riscv32` and `agora_rtc/PACKAGE_INFO`. All three pinned RTSA file hashes
  remain unchanged.
- Audio lifecycle, PCM buffer, display, input/board, HTTPS trace and build/delivery host tests
  passed. The build/delivery suite ran all 52 cases under Python 3.12.
- Clean ESP-IDF v6.1 preview builds passed for `zh-CN` and `en-US`. Size checks show 38% and
  36% application-partition headroom respectively. Both merge/check flows verified four flash
  segments, RISC-V ELF output, bundled RTSA fingerprints and local AOSL symbol ownership.

| Language | App bytes | Merged bytes | Merged SHA-256 |
| --- | --- | --- | --- |
| zh-CN | 2568144 | 2699216 | `0bb7d8970d624af5604dcfced281510e027438533977d77ea3d956927b16336b` |
| en-US | 2648800 | 2779872 | `f1844cac0ab9f446bf412d5085227101f42adea2a80164fa91162e1dfeeff1bd` |

These hashes describe local images built from the dirty `main` worktree at
`9a796a84d82ff6ab4cd4206571d6f46c95393b6f`. They are not release artifacts and do not
establish remote CI or new hardware results.

## 2026-11-03 standalone listen translation patch preparation — host/build only

- Build source: `/private/tmp/mybot-s31-listen.91FRru`; Chinese build directory `build/listen-zh-ready`.
- API: `https://mybot.sh3t.agoralab.co/api`; initial input `en-US`, output `zh-CN`; cloud AEC, AI QoS and fast-send multiplier 3 enabled; G.722 / 16 kHz / 60 ms.
- ESP-IDF v6.1 preview S31 build, size and merge-bin passed. The component manager's parent-process inspection is blocked in this session; the build used already-resolved component sources with `IDF_COMPONENT_MANAGER=0`, explicit extra component directories and an external canonical-name alias for the unchanged touch dependency. The Wi-Fi wrapper now explicitly declares its IDF 6 cJSON dependency.
- `check_s31_build.py` passed RISC-V ELF, local AOSL ownership, unchanged bundled RTSA fingerprints, video-off and four flash segments.
- Client: 390 protocol/allocation-fault checks with AEC/QoS on and off. Lifecycle: 26 failure/recovery scenarios. RTC: 9 source-level subscription/callback scenarios. Control: 7 result-mailbox/shutdown scenarios. State model: 141 assertions. These ran with UBSan.
- Both-language UI/button tests and Chinese font coverage passed; input 13 key + 5 board cases, display 23 cases, TDM audio lifecycle, PCM buffering and 20 HTTP trace cases passed. Build/delivery Python tests: 52 run, 5 skipped under Python 3.10 (the batch builder's safe-cleanup tests require 3.11+).
- ASan is unavailable on this host: even an independent minimal program terminates with Illegal instruction. It is not reported as passed.
- Firmware source and patch are complete in the writable directory. Automatic approval rejected writing the original S31 worktree because this session exposes it as read-only and disallows escalation. The original untracked API document and simulator clipping were preserved. The patch is checked against the original worktree and can be applied after write access is available.
- Physical translation, cloud AEC effectiveness, route publisher behavior, language controls, cancellation and reconnect remain pending real-device validation. Earlier ordinary conversation audio tests do not establish those results.

Merged image: `esp32-s31-listen-zh-CN.bin`, 2572064 bytes, SHA-256 `8c287ca15037d11d2d745da3fa79a36f1b67651e2a1d69fcd374f35e0fa1c7b8`. Application size: 2440992 bytes.

## 2026-11-03 original-worktree Chinese build — Chinese to English

- Applied `/private/tmp/s31-listen-translation.patch` in `/Users/shangjinlong/Desktop/workspace/github/mybot-esp32s31`, preserving the pre-existing untracked API document and simulator clipping. The user authorized the translation SDK fork.
- Changed the firmware defaults to source `zh-CN` and target `en-US`, retaining Chinese UI and the development API above. Existing valid NVS language selections still take priority; they can be changed through the UI.
- Regenerated `build/listen-zh/sdkconfig` from project defaults. Direct ESP-IDF v6.1 preview `build`, `size` and `merge-bin` passed in the original worktree. The component manager ran normally with permission to inspect parent processes, using the resolved managed sources; no external component alias or disabled-manager fallback was needed.
- All 11 translation/inherited host suites passed, including UBSan coverage. Formatting, SPDX, workflow parsing, shell syntax and `git diff --check` passed. Build/delivery tests: 52 run, 47 passed and 5 Python 3.11+ safe-cleanup cases skipped under IDF Python 3.10.19.
- The final delivery checker verified RISC-V ELF, local AOSL ownership, unchanged pinned RTSA fingerprints, video disabled and all four merged flash segments. Configuration checks confirmed Chinese-to-English defaults, cloud AEC, AI QoS, fast-send multiplier 3 and 60 ms audio. Each runtime component is registered once.
- Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2572064 bytes, SHA-256 `d472bb3206bfaf1e3a9ef852cd7dd016e8f576ba15716082b447ec08d96511b8`. Application size: 2440992 bytes, with 41% of the application partition free. The adjacent manifest, checksum and flash instructions describe this build and supersede the patch-preparation image above.
- No flashing, translation hardware test or remote CI run was performed. The translation hardware validation items above remain pending.

## 2026-11-03 HTTPS diagnostics and translation startup

- User-supplied serial output shows two complete HTTP 201 translation-start responses followed by local cleanup, then a third start with a complete sent request but no response bytes before `start_unknown`. SNTP synchronization appears later. The earlier firmware did not log the validation rejection reason, so the exact cause of those first two rejections is not proven by that trace. The user separately confirmed all cloud sessions have ended. No raw credentials, device IDs or parent IDs from that trace are retained here.
- HTTPS diagnostics now cover all requests, including pairing, binding-status and translation start/stop. Logs retain bounded PSRAM capture, connection ID, UTF-8/hex body chunks and completeness, and add method, redacted path, accepted/captured wire bytes, empty-body records and transport-error state. Headers are not logged. Capture/formatting failures remain diagnostic only.
- Translation start now rejects an unusable UTC clock before HTTP with `TIME_NOT_SYNCED`, preserving retry after synchronization. Post-response validation still rejects expired/malformed credentials and retains known parents for cleanup. Lifecycle logs include the rejection/cleanup reason. Unknown starts remain durable and are never retried automatically.
- Added a request-specific shared 30-second deadline for translation start; default GET, pairing and stop requests retain 5 seconds. Fake TLS/tick tests verify responses after 5 seconds, decreasing budgets across connection/short writes/fragmented reads, partial response failure and rejection of nonpositive timeouts. DNS and ESP-TLS internal operations can exceed the nominal SDK budget; no strict real-device wall-time bound is claimed.
- All 12 host suites passed with UBSan: HTTP deadlines 10 scenarios, body trace 31, protocol/allocation faults 202 + 198 with AEC/QoS on/off, lifecycle 27, control 7, RTC 9, state 141 assertions, both UI languages and 92 Chinese glyphs, display 23, input 13 + board 5, TDM audio and PCM buffering. Formatting/SPDX, workflow YAML/shell checks and `git diff --check` passed. ASan is not reported as passing on this host.
- Direct ESP-IDF v6.1 preview Chinese `build`, `size` and absolute-path `merge-bin` passed in the original worktree. The final checker verified RISC-V ELF, local AOSL, pinned RTSA fingerprints, video disabled and four flash segments. Chinese UI, Chinese-to-English defaults, development API, cloud AEC, AI QoS, multiplier 3 and 60 ms remain configured.
- Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2574592 bytes, SHA-256 `3cd8fda69490c8ed8f324122a104a9e01d3c5da77ae7e19c2b10e8bd50ff2cee`. App size: 2443520 bytes, with 41% application-partition headroom. Adjacent metadata supersedes prior image hashes.
- Verified the merged image contains erase bytes throughout NVS at `0x9000..0xcfff`. Once cloud sessions are confirmed ended, full-image flashing from `0x0` clears the local unknown journal and also resets Wi-Fi, binding, language and volume settings. Ordinary segmented flashing preserves NVS. No device was flashed here; successful translation/AEC and recovery still require real-device validation.

## 2026-11-03 initial UTC recovery

- A subsequent user-supplied trace confirms successful NVS erasure, pairing HTTP 201, binding status becoming `bound` and two local `TIME_NOT_SYNCED` rejections. Both report no cleanup obligation or unknown start, so the previous unknown journal has been cleared. The trace shows SNTP started but no completed time synchronization; translation was not sent to the service. No credentials or device identifiers from that trace are retained here.
- Added independent HTTPS Date observation, bounded to 2 KiB of response headers inside the existing PSRAM connection allocation. A complete 2xx header with a strict, consistent GMT Date in 2020–2099 can initialize an unset clock. Missing, malformed, conflicting, oversized or non-2xx headers have no effect on transport. The parser does not store response bodies or depend on body diagnostics.
- Clock check and update run synchronously in the lwIP context through `esp_netif_tcpip_exec`, serializing against SNTP; a previously completed SNTP update is preserved. Existing TLS chain/hostname verification settings remain unchanged (`CONFIG_MBEDTLS_HAVE_TIME_DATE` remains disabled as before). No extra request or new task is used.
- Configured `ntp.aliyun.com`, `time.cloudflare.com` and `pool.ntp.org`; regenerated the Chinese sdkconfig with `CONFIG_LWIP_SNTP_MAX_SERVERS=3`. A stale smaller configuration now produces a clear CMake error. The previous generated config is retained as ignored `build/listen-zh/sdkconfig.before-time-sync`.
- All 13 host suites passed with UBSan, including 267 new Date/calendar/header/fragmentation/current-clock/failure/queued-SNTP cases. Inherited protocol, lifecycle, ownership, audio, input/display, UI and playback checks passed. Formatting/SPDX, workflow YAML/shell checks and `git diff --check` passed.
- Direct ESP-IDF v6.1 preview Chinese build, size, merge and delivery validation passed. RISC-V, local AOSL, pinned RTSA, four flash segments, video off, Chinese-to-English defaults and 60 ms/cloud-AEC settings remain verified.
- Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2576960 bytes, SHA-256 `1090aad3a8b06111f954f45a470059329ec6e7742883012651dc98ce3d3a2c48`. App size: 2445888 bytes; 41% application-partition headroom. Use segmented `idf.py flash` to retain the user's restored pairing and network settings.
- These new time paths and successful translation still require a real-device retest. No board was flashed here and no remote CI run was performed.

## 2026-11-03 complete body in one log record

- At the user's request, each captured request/response body now emits exactly one `phase=data` record, including empty bodies. Text uses the existing capture buffer directly. The 256-byte loop and `offset` field have been removed; begin/end metadata remains.
- NUL-containing bodies use a temporary PSRAM buffer for one complete hex record. Allocation failure emits one explicit unavailable record and frees captured buffers without changing the HTTP result. Capture remains bounded to 2 KiB requests and 32 KiB responses including headers; overflow prints all captured body bytes once with incomplete/capacity metadata.
- Body-trace UBSan tests passed 33 synthetic scenarios, including 12 KiB UTF-8, exact capture boundaries, overflow comparison, binary hex and temporary-allocation failure. The 267 UTC bootstrap and 10 HTTP deadline regressions also passed. Formatting, SPDX and `git diff --check` passed.
- Direct ESP-IDF v6.1 preview Chinese build, size and merged delivery verification passed, including RISC-V/local-AOSL/pinned-RTSA/no-video checks. Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2576976 bytes, SHA-256 `a70dbf3da62a4ea61ed297d3994a16109037630528d984619b168588b611ce8c`. App size: 2445904 bytes, with 41% application-partition headroom.
- The current target uses newlib `vprintf` through IDF log version 1, with no project output override or fixed log-message length buffer. UART output duration scales with body size; serial presentation and translation behavior were not hardware-tested in this update. Segmented flashing preserves the current pairing and network settings.

## 2026-11-03 active output volume display

- Fixed the reported constant 100% UI value: the presenter previously read software PCM gain, whose default is unity/100%, while S31 volume controls use the hardware driver. A shared media-pipeline getter now reads the same active output channel used by volume adjustments, including hardware values restored from NVS. Hardware set/persistence and PCM/AEC behavior are unchanged.
- Failed or out-of-range hardware reads produce an unknown display value (`--`) rather than a software gain value. Unknown values retry once per ten 100 ms control ticks and refresh after recovery; healthy values are not polled.
- All 14 host suites passed with UBSan. The new volume suite covers eight production app/key/timer-to-pipeline/audio/presenter/LCD scenarios: initial/restored 60/40 with software gain 100, adjustments, 0/100 saturation, read/write failure, invalid values, software fallback and timed recovery. These tests simulate platform volume operations, not physical loudness or real NVS persistence.
- Formatting/SPDX, `git diff --check`, workflow YAML and shell checks passed. Direct ESP-IDF v6.1 preview Chinese build, size, merge and delivery validation passed, retaining RISC-V/local-AOSL/pinned-RTSA/no-video and Chinese-to-English/60 ms/cloud-AEC settings.
- Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2577104 bytes, SHA-256 `b5a53dd9648587b0734c703722b30c0070402a96ede31245816689c4fa46933e`. App size: 2446032 bytes, with 41% application-partition headroom. Use segmented `idf.py flash` to preserve pairing, network and stored volume.
- No board was flashed here. Actual display synchronization with physical and touch volume controls still requires the user's real-device retest.

## 2026-11-03 native simulator scene UI

- Implemented the approved 800×480 LVGL adaptation of the user-supplied HTML simulator: home, scene cards, media settings, a shared 15-language picker, live media, menus, confirmation/completion and workflow/error pages. Face-to-face and meeting modes provide settings previews with disabled business starts. Captions/full text/history and brightness are labelled demonstrations; Wi-Fi, time, hardware volume and session/pause status use live state. No external browser, icon or image runtime was imported.
- Added a public fixed-capacity UI command mailbox for language pairs, absolute volume and local pause. The platform copies validated released gestures to the existing key worker; the SDK control owner applies commands and publishes actual LCD snapshots. Language pairs persist together only while idle. Allocation/queue/state changes cannot create optimistic business success.
- Pause retains capture and the RTC parent, continues the existing 16 kHz mono S16/60 ms MIC/reference uplink as silence, and independently stops playback to clear SDK and driver PCM. Pipeline-owned bounded worker fences and a nonblocking remote producer gate protect pause/resume. Failures retain pause; incomplete end-session recovery blocks a new cloud start until local media is restored. Hardware volume and the TDM/codec drivers remain unchanged.
- Review and regressions cover late successful start after cancel, safe STOP delivery across STARTING→ACTIVE/cleanup, a visible stop retry, DELETE callbacks during page replacement, explicit READY-state errors, stale epochs, copied commands and failed recovery. UI actions never clear the durable unknown/cleanup journal.
- Regenerated the pinned Noto Sans SC subsets at 20/28/40 px: 316 characters per size (221 non-ASCII plus printable ASCII), 326781 bitmap bytes. The original source font SHA and OFL notices are unchanged. Nine actual LVGL CPU-framebuffer renders were visually checked; ignored PNG previews and their scope note are in `build/ui-preview/`. These are host renders, not photographs of a device.
- All 15 host suites passed with UBSan. New/expanded coverage includes media pause 11 scenario groups, UI control 8 plus existing mailbox 7, lifecycle 28, input 15 plus board 5, the new two-language UI and three font sizes, display 23, and inherited audio/PCM/protocol/deadline/time/body tracing. Builder/delivery tests ran 52 cases under IDF Python 3.10: 47 passed and 5 Python 3.11+ cleanup cases skipped. Formatting/SPDX, YAML/shell checks and `git diff --check` passed.
- Direct ESP-IDF v6.1 preview Chinese `build`, `size` and absolute-path `merge-bin` passed after final integration. The shared checker verified RISC-V ELF, local AOSL ownership, immutable RTSA fingerprints, four flash segments and video disabled. Configuration remains Chinese UI, Chinese→English defaults, cloud AEC, AI QoS, multiplier 3, 60 ms and three SNTP servers.
- Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2899488 bytes, SHA-256 `61dc03d5a48dbd22535e994789fcd59ad890180ea3f2497323d1262d6a9961df`. App size: 2768416 bytes, with 33% application-partition headroom. Native segmented flashing preserves pairing, Wi-Fi, language and volume settings.
- No board was flashed and no remote CI was run. Real touch gestures, layout, translation audio/AEC, local pause/resume and cancellation/recovery require device acceptance; earlier basic audio/display reports do not establish these new paths.

## 2026-11-03 confirmed stop and completion UI

- A user-provided serial trace reports RTC connection, explicit translated-publisher subscription, local RTC teardown and a complete HTTP 200 stop response with `conversation.status="ended"`, followed by the SDK cleanup-confirmed log. The user reported that the screen nevertheless showed a startup-error message with `CLEANUP_PENDING`. No credentials, device identifiers or parent IDs from the trace are retained here.
- Fixed the SDK cause: successful durable pending-record removal now clears only temporary cleanup status codes (`START_UNKNOWN`, `CLEANUP_PENDING`, `CLEANUP_AUTH_REJECTED`) before publishing idle. Stop retries preserve genuine start/storage errors rather than replacing them with a generic cleanup hint. Failed durable cleanup still retains the pending record and cannot report completion.
- The UI immediately describes a requested stop as ending. Actual stopping/cleanup states retain their dedicated message and retry behavior. Only after SDK `READY`, stale cleanup hints cannot override the completion/setup page or create a false startup error; real startup, clock, media and storage failures remain visible.
- Production lifecycle fault tests passed 29 scenarios with callback snapshots of state, error and pending flags. The old cleanup-status logic and old error-overwrite logic each failed the new assertions in separate temporary-source counterchecks. Both UI languages passed UBSan state-sequence tests, including stop/retry/success, cancelled late start, change-after-stop, stale cleanup hints and preserved real errors. Result mailbox/control (7 + 8) and volume (8) regressions passed; font coverage, formatting/SPDX and `git diff --check` passed.
- Actual LVGL CPU framebuffer renders of ending, retryable cleanup and completion were checked; ignored previews are in `build/ui-preview/cleanup-contact-sheet.png`. They are host renders, not hardware photographs.
- Direct ESP-IDF v6.1 preview Chinese build, size, merge and delivery checks passed, preserving RISC-V/local-AOSL/pinned-RTSA/no-video and Chinese-to-English/AEC/QoS/60 ms configuration. Image recorded for this revision: `releases/esp32-s31-listen-zh-CN.bin`, 2899792 bytes, SHA-256 `6fcfedeb6ebe8f323d393081e9ba4eaf10b516f7c0cace713931dfa2a1412612`. App size: 2768720 bytes, with 33% partition headroom.
- No board was flashed here; the corrected end-page transition still requires the user's device retest. Segmented flashing preserves pairing and Wi-Fi settings.

## 2026-11-03 RTC session ownership, language selection and documentation

- Each translation now finalizes RTSA and its forwarding queue. The next session initializes from its own returned App ID. Connection leave/destroy failures are recoverable when global finalization succeeds; failed global finalization remains terminal until reboot, while forwarding-queue cleanup can be retried. New cloud starts first check that local RTC cleanup has completed. `RTC_NOT_READY` survives cloud cleanup confirmation and does not overwrite an existing genuine failure during stop.
- Language selection submits through the existing copied, nonblocking public UI API. Both language selectors and start remain disabled until the requested pair is read back or a real failure/state change occurs. A local timer, unrelated volume update or unchanged old error cannot acknowledge it. Rejected admission permits immediate retry.
- Corrected both volume-persistence failure log arguments and enabled compiler printf-format checking in the shared host logger declaration. Updated both README and contribution/board guides, public lifecycle/state comments and maintained local documentation links to the current translation fork and UI.
- All 15 host suites passed. RTC now covers 17 scenarios, including project changes, partial connection teardown, terminal vendor finalization and retained forwarding queues. Control covers 7 result-mailbox cases and 11 UI/control cases, including preservation of real errors when RTC cleanup fails. Both UI languages and font coverage passed; formatting/SPDX, maintained local links and `git diff --check` passed.
- Direct ESP-IDF v6.1 preview builds used independent `build/listen-zh` and `build/listen-en` sdkconfig directories. Chinese and English builds passed with Chinese-to-English defaults, AEC/QoS and 60 ms audio. App sizes are 2769408 and 2850304 bytes respectively, retaining 33% and 31% application-partition headroom. Link/configuration checks retain RISC-V, local AOSL, pinned RTSA and disabled video. Delivery hashes and segment metadata are generated alongside the images under `releases/`.
- No device was flashed and no remote CI was run. Repeated real-device sessions, cross-project rebinding, delayed language changes and fault recovery still require hardware acceptance. Use segmented `idf.py flash` to preserve NVS settings.

## 2026-11-04 complete local CI checks

- Fixed the input host fixture's missing `<cstring>` include, which Clang's transitive headers had concealed and GNU C++ rejected. A full GNU compiler run also found that the HTTPS-only HTTP test retained unused TCP helpers at `-O0` and failed to link. Its runner now uses function/data sections and the same Darwin/Linux linker garbage collection as the existing RTC/control runners.
- Executed the workflow's complete format and SPDX scopes with clang-format 18.1.8, whitespace checks and all 17 shell-block syntax checks. All 15 host suites passed on Apple Clang 17 and genuine GNU GCC/g++ 13.4. The display, input and native UI suites also passed with native Clang UBSan enabled. Python 3.11.7 ran all 52 builder/delivery tests with zero skips, including safe cleanup.
- Darwin's GNU compiler does not supply libubsan. GNU runs with the runners' original CI flags passed using the LLVM 17 UBSan runtime, in addition to all 15 ordinary GNU compiler runs. Extra C++ vptr instrumentation with this mixed runtime fails an independent `std::thread` program, so that combination is not claimed as validated. No test sanitizer setting was weakened in the repository.
- Direct ESP-IDF v6.1 preview Chinese and English builds, size reports, image merging and delivery checks passed in independent build/sdkconfig directories. The pinned RTSA fingerprints, RISC-V/local-AOSL link, disabled video, Chinese-to-English defaults, 60 ms frames and AEC/QoS settings passed their checks.
- These are local macOS results. Ubuntu 24.04/Python 3.12 GitHub-hosted execution and artifact upload were not performed locally; no board was flashed. Host checks, firmware checks and hardware results remain separate.

## 2026-11-04 AI QoS default disabled

- Firmware Kconfig/defaults and the SDK fallback now disable AI QoS. Explicit opt-in remains available. The client sends `ai_qos=false` and omits the fast-send multiplier when disabled; the RTC audio option is also false.
- Existing protocol tests passed both explicit feature states (202 + 198 cases). RTC tests passed 17 scenarios with the new default and another 17 with AI QoS explicitly enabled. Full format/SPDX scopes and whitespace checks passed.
- Direct Chinese and English ESP-IDF v6.1 preview builds passed in fresh `build/listen-zh-qos-off` and `build/listen-en-qos-off` directories. Both merged images passed link and delivery checks. Both generated configurations disable AI QoS, and both client and RTC compile commands contain `MYBOT_AI_QOS=0`. Cloud AEC remains enabled. Those directories record this earlier validation; subsequent local builds use `build/korvo-s31`. No device was flashed.

## 2026-11-04 fixed local build directory

- The user requires all subsequent local firmware builds and generated sdkconfig to remain under `build/korvo-s31`. Agent guidance and current development/build/flash instructions now use that path. CI job directories and historical records are separate.
- Rebuilt the current Chinese firmware in that directory using the existing configuration and normal ESP-IDF configuration migration. Verified the development API, Chinese-to-English defaults, AI QoS disabled in generated configuration and service/RTC compiler definitions, cloud AEC enabled, 60 ms audio and three SNTP servers.
- The application build, partition-size and merged-image link/delivery checks passed: 2769360 application bytes, with 33% application-partition headroom. The merged image and its metadata are also under `build/korvo-s31`. The agent did not flash a device. The original generated configuration was preserved in a private temporary backup.
- The user subsequently reported that testing passed for this build, separately from the local build and image checks above.

## 2026-11-04 RTM translation captions

- Replaced the fixed caption demonstration with trusted RTM `user.transcription` and `user.translation` messages for the active listen route. The application checks sender, channel and session generation, copies borrowed packets into an eight-message bounded PSRAM inbox, and uses its existing control tick to merge revisions. No new worker or persistent caption storage was added. Invalid-source/JSON and inbox-overflow diagnostics report counts without logging caption text or credentials.
- Originals and translations share a 64-entry PSRAM ring, paired by stream/turn. Each side retains at most 1,023 UTF-8 bytes with character-boundary truncation, independent final flags and optional relative timing. Final-to-final corrections remain accepted; later interim text cannot replace final text. Equal or absent timing uses arrival order. An already evicted turn has no unbounded deduplication record and can be treated as new if it arrives again.
- Live captions update existing labels; full text scrolls, and history pages retain selection. An open full-text page keeps its copied sentence if the ring evicts it. Stop closes admission, drains accepted tail packets and retains history; a new media session clears it. Public reads copy into caller-owned storage without waiting and reject stale generations. Caption allocation failure leaves audio available and shows captions unavailable.
- Added reproducible Noto Sans SC caption fonts: 20 px/2 bpp covering all 6,763 GB2312 Han characters, Latin-1 and common punctuation, plus a 28 px Latin-1 font with Chinese fallback. Total caption font data is 744,719 bytes. The static 20/28/40 px UI subsets now contain 317 glyphs per size, including 222 non-ASCII characters. First-release typography targets common Chinese/English; the 15 audio language choices do not imply complete glyph or layout support for all scripts. License and provenance records were updated.
- All 16 workflow host suites passed with Apple Clang 17 and GNU GCC/g++ 13.4. New caption-model tests cover 295 checks; application routing, ownership, generation and allocation-fault tests cover 8 cases/205 checks. RTC tests cover 23 scenarios with AI QoS off and another 23 with it enabled, including string/binary payloads and stale callbacks. Final control/UI changes were rerun with both compilers. GNU uses the runners' original CI flags and the LLVM UBSan bridge described above; no additional mixed-runtime C++ vptr validation is claimed.
- All 52 Python 3.11 builder/delivery tests passed with zero skips. The complete workflow formatting/SPDX scopes use clang-format 18.1.8; workflow shell syntax, maintained local documentation links and `git diff --check` passed. Actual LVGL CPU-framebuffer previews and a real 2 bpp Chinese/Latin-1/fallback font sample were checked; these are host renders, not device photographs.
- Direct ESP-IDF v6.1 preview Chinese `build`, `size`, absolute-path `merge-bin` and delivery checks passed in `build/korvo-s31`, preserving its existing sdkconfig. The checker verified RISC-V ELF, local AOSL ownership, unchanged pinned RTSA fingerprints, disabled video and all four flash segments. Chinese-to-English defaults, 60 ms audio, cloud AEC and disabled AI QoS remain configured. The application is 3,521,888 bytes, with 15% partition headroom reported by IDF.
- Merged image: `build/korvo-s31/mybot_esp32s31-merged.bin`, 3,652,960 bytes, SHA-256 `052e5fb4ed005688c35de3510b89b0a4297c4febe2634c8c73935a6f94989bbe`. Adjacent manifest, checksum and flash instructions identify this build. Use segmented `idf.py --preview -B build/korvo-s31 -p PORT flash monitor` to retain NVS settings.
- The agent did not flash a device or run remote CI. The user subsequently reported that this caption firmware passed testing. This is user-reported acceptance, separate from the local host/build results above; no detailed caption trace, per-page results, endurance or resource measurements were supplied.

## 2026-11-04 face-to-face translation

- Implemented the user-approved two-RTC/one-RTM design. Media requests `listen`; face-to-face requests `listen_speak`, with the stored left/right language pair interpreted as my/other languages. The client normalizes returned routes by direction, checks a shared App ID/channel, four distinct identities, direction languages, publisher roles and expiry. A listen route may be captions-only; speak requires translated audio. Mode is held in memory and defaults to media after SDK restart. No new persistent record was introduced.
- Each parent session owns one RTSA service and forwarding queue. Face-to-face joins two RTC connections with their own returned accounts/tokens; only the listen account logs into RTM and subscribes to the common channel once. Both trusted Agent publishers feed the caption inbox. Audio becomes ready only after all RTC joins and required publisher subscriptions succeed. Connection/publisher readiness and reconnect use a 15-second deadline checked by the existing RTC queue; a missing route or required Agent failure closes all audio and triggers the existing parent cleanup.
- Face-to-face starts with microphone content closed until a language button selects listen or speak. The sender consumes one physical capture/reference frame, sends real MIC only to the selected route, and gives the other route silent MIC with the same playback reference. Both preserve 16 kHz mono S16, G.722 and 60 ms cloud-AEC framing. Idle-route silence is not claimed to override the cloud Agent's idle timeout. The extra frame scratch is PSRAM-allocated on first face-to-face use and reclaimed only after media destruction succeeds.
- Direction changes use the existing pause/worker fences to discard local capture, send and playback buffers before resuming. A separate bounded control gate covers remote PCM route admission, so a producer admitted for the previous direction cannot publish through the switch. The speaker accepts only the selected direction. Pause/resume or gate failure leaves an explicit retryable direction error; stop admission cancels the operation. No server-side TTS cancellation or text-final-to-playback-end inference is assumed.
- Captions use `(direction, stream_id, turn_id)` within one shared 64-entry history. Live follows the selected direction's latest retained sentence; late captions from the other Agent retain their own direction. Full text and history show language direction, and stop retains history. The UI waits for SDK mode/direction readback; a small selection completion counter distinguishes a repeated failure from unrelated volume/caption updates. Meeting remains a preview.
- All 16 workflow host suites passed with native Apple Clang 17 and GNU GCC/g++ 13.4. Native Clang also enabled UBSan for the display/input/UI runners; GNU used the original CI flags and LLVM UBSan bridge described above. Coverage includes 399/391 client checks with feature switches, 31 lifecycle scenarios, 48 RTC scenarios, 387 caption-model checks, 7 result-mailbox and 13 UI/control scenarios, 9 caption-routing cases/223 checks, and 10 face-to-face routing/fence/fault cases in each cloud-AEC configuration. Existing volume, audio, pause, HTTP, clock and PCM regressions passed.
- All 52 Python 3.11 builder/delivery tests passed with zero skips. Full clang-format 18.1.8/SPDX scopes, workflow shell syntax, local documentation links and whitespace checks passed. The UI fonts were regenerated from the same pinned Noto Sans SC input: 319 glyphs per size, including 224 non-ASCII characters. Twelve actual LVGL CPU-framebuffer states were reviewed, including both speech directions, pending selection, pause, full text, history and completion. These previews are host renders.
- Direct ESP-IDF v6.1 preview Chinese build, size, merge and delivery checks passed in `build/korvo-s31`, preserving its existing sdkconfig. RISC-V ELF, local AOSL ownership, pinned RTSA fingerprints, video disabled and four merged flash segments were verified. Chinese UI, the Chinese/English default pair, cloud AEC, 60 ms audio and disabled AI QoS remain configured. The application is 3,533,008 bytes, with 14% partition headroom reported by IDF.
- Merged image: `build/korvo-s31/mybot_esp32s31-merged.bin`, 3,664,080 bytes, SHA-256 `3d839b82a229d8e34a4e3751278a63c2170419c312d441432068583bb42a4ad6`. Adjacent delivery metadata identifies this build. Segmented `idf.py --preview -B build/korvo-s31 -p PORT flash monitor` preserves NVS settings.
- The agent did not flash a device or run remote CI. The user subsequently reported that this face-to-face firmware passed testing. This is user-reported acceptance, separate from the host/build checks above; no detailed per-scenario trace, echo/reference timing, idle-direction lifetime or sustained resource measurements were supplied. The supplied RTSA library/header and external reference sources remain unchanged.

## 2026-11-04 native USB CDC probe

- Added the opt-in `MYBOT_USB_CDC_PROBE` diagnostic to the existing Chinese firmware. It starts a native CDC echo interface before network setup and preserves the UART0 console and translation application. Its product is `MyBot S31 USB Probe`, with one CDC interface and no audio, storage, HID or network class. The default production configuration leaves the diagnostic disabled.
- Pinned managed `esp_tinyusb 2.4.0` and `tinyusb 0.19.0~3`, both explicitly supporting S31, through the application manifest and IDF-generated lock. The wrapper selects S31's HS-capable port 0 and UTMI PHY, with internal DMA buffers and its built-in FS/HS/qualifier descriptors. Managed sources and the pinned RTSA/AOSL/codec inputs were not edited. Dependency provenance and notices were updated.
- A single application worker owns a 512-byte pending buffer and all CDC I/O. Partial/zero writes preserve the remaining suffix and apply backpressure; nonblocking flush never causes already-queued bytes to be resent. Callbacks only update short link state and notify the worker. Mount/DTR/detach generations discard stale application pending data, while suspend/resume preserves a pending transfer. FIFO clearing does not retract in-flight endpoint or host data. Failure cleanup stops the USB task before releasing CDC callback state; incomplete teardown retains ownership and prevents another startup attempt during that boot.
- The macOS checker validates the complete USB identity and native CDC path before opening it, rejecting CP2102N/UART even with an explicit port. Two rounds test 134,544 bytes with random data, control characters, packet boundaries and 64 KiB challenges. Reads and single-attempt nonblocking OS writes are interleaved under one total deadline; corruption, missing or extra bytes fail the check. `--list` only reads enumeration metadata.
- All 17 workflow host suites passed locally. The new C suite passed 20 scenarios/1,352 checks on Clang UBSan and GNU GCC 13 UBSan; the new standard-library Python suite passed 17 cases, and all 52 Python 3.11 build/delivery tests passed with zero skips. Full clang-format 18.1.8/SPDX scopes, workflow shell syntax and whitespace checks passed. These tests mock USB/OS boundaries rather than establishing a real data connection.
- Both diagnostic-on and production-off Chinese configurations built successfully with direct ESP-IDF v6.1 preview in `build/korvo-s31`. The official `kconfserver` temporarily disabled the diagnostic and all USB classes, then restored the saved diagnostic configuration; unrelated settings were preserved. Production-off application size remains 3,533,008 bytes. The final directory is diagnostic-on: 3,566,896 application bytes, with 14% partition headroom reported by IDF. Chinese UI, cloud AEC, disabled AI QoS and 60 ms audio remain configured.
- The final merge/delivery checker verified RISC-V ELF, local AOSL ownership, unchanged RTSA fingerprints, video off and four flash segments. Image: `build/korvo-s31/mybot_esp32s31-merged.bin`, 3,697,968 bytes, SHA-256 `189d1dd32276b562b7362580de18646297e9597cddbf1e01110d7e2ab241a5ea`. Eleven pre-probe configuration/firmware files remain in `build/korvo-s31/before-usb-probe`; their backup hashes were verified for rollback.
- Before flashing this diagnostic, read-only macOS inspection found the existing CP2102N bridge and `/dev/cu.usbserial-14410`, with no matching native probe or S31 audio device. The checker `--list` also found no matching probe. No physical port was opened by the checker, no board was flashed and no remote CI was run in this update.
- The user confirms an unmodified factory board. Its J2 host VBUS is hardwired on; this firmware has neither board power control nor host VBUS monitoring. An apparently uneventful direct cable connection or a future CDC pass does not establish electrical safety, reliable physical-detach detection, UAC isochronous audio or meeting translation. See [probe operation and rollback](USB_CDC_PROBE.zh-CN.md).
- The user subsequently flashed the diagnostic through the CP2102N bridge; the supplied log reports verified application data, TinyUSB installed on port 0, and normal display/network/audio startup. For roughly 70 seconds it reports `mounted=0`, no mount events and zero application RX/TX bytes. The user confirms a normal A-to-C cable directly from board J2 to a Mac USB-C port. Read-only Mac inspection and the checker also find no Probe device. Enumeration and CDC transfer have therefore not passed; the log does not identify whether any USB reset/control requests occurred. The cable's initial Type-C role assignment and the board's host VBUS wiring need to be resolved before treating this topology as a valid device test connection. No raw device identity, network identifiers or credentials from the log are retained here.

## 2026-11-04 USB enumeration diagnostics

- A second user-provided log after changing the connection still reports no configuration/mount or application data over approximately 21 seconds. The CP2102N path changed to `/dev/cu.usbserial-1430`; read-only Mac enumeration still finds no native Probe. This verifies continued software startup but does not establish the host role, electrical continuity or the stage at which enumeration stops.
- Source inspection confirmed S31 port 0/UTMI mapping, automatic Device connect during TinyUSB initialization, and separate native OTG pads from LCD GPIO33/34. No corresponding initialization fix was established. The original `mounted` counter only observes completed configuration; it cannot distinguish absent host traffic from an earlier control-transfer failure. TinyUSB's interrupt allocation return is also not propagated through driver installation, so installation success alone does not establish IRQ allocation.
- Added `diag=2`: the public TinyUSB event hook counts bus-reset, SETUP, transfer-complete and unplug events using the appropriate ISR/task critical section. The ISR performs no logging, allocation, FIFO access or task notification. Existing worker logs also query stack initialization/connection/configuration state.
- Added project-local, read-only S31 PHY/controller snapshots and one public IDF interrupt allocation dump at startup. Snapshots cover clocks, actual mode, force-mode bits, soft disconnect, device address, internal session flags, frame/line state, clock gating and interrupt masks. They avoid FIFO/pop and interrupt-status registers and perform no register writes. Disabled clocks cause controller reads to be skipped. Internal session-valid and reset speed values are explicitly not treated as connector voltage or negotiated speed measurements.
- USB host regression passed 23 scenarios/1,578 checks on Clang UBSan and GNU GCC 13 UBSan, including ISR side-effect restrictions, event routing, counter wrap, and one-time interrupt diagnostics. Changed C sources/mocks passed clang-format 18.1.8 and SPDX checks; whitespace checks passed. The real S31 snapshot code compiled against the installed IDF headers; host tests do not emulate its registers.
- Chinese ESP-IDF v6.1 preview build/size/merge and delivery checks passed in `build/korvo-s31`, retaining the enabled probe, Chinese UI, disabled AI QoS, cloud AEC and 60 ms audio. RISC-V/local-AOSL/pinned-RTSA/no-video/four-segment checks passed. App size: 3,570,272 bytes; IDF reports 14% partition headroom. Merged image: 3,701,344 bytes, SHA-256 `22263ba0d3e176ae72b8e2f6940c93618783248728a1758e134c147bf2fbe63d`, at the same `build/korvo-s31/mybot_esp32s31-merged.bin` path.
- The agent did not flash this revision, open the UART/CDC data ports, or alter the board's power connection. Enumeration remains unverified. The second diagnostic is intended to collect the missing evidence before choosing a firmware fix or attributing failure to a cable or PHY.
- The user subsequently supplied diag=2 output: the controller reports Device mode, enabled clocks, cleared soft-disconnect and an allocated USB_OTGHS interrupt, with address/frame and TinyUSB event counters remaining zero. Unplugging only J2 changed the reported line state from 3 to 1 and suspend status from 0 to 1. This establishes a register-level response to connection changes, not successful enumeration or a physical voltage measurement. TinyUSB's reset event is reported at enumeration completion rather than every raw USBRST assertion. The exact factory example at esp-dev-kits `5c06bf1835329d6525f316475f7218aab6acfe79` uses HID Host with default PHY initialization; it provides a Host comparison, not a Device-mode fix.

## 2026-11-04 USB Host phone enumeration probe

- The user has an Android phone and A-to-C data cable but no wired mouse or USB flash drive; a bus-powered portable disk would add a power-load variable. Added a default-off `MYBOT_USB_HOST_PROBE`, mutually exclusive with CDC, using the pinned official `espressif/usb 1.6.0`. Its single CPU0 worker runs the default PHY/Host initialization and services library/client events, printing cached VID/PID, speed and device/configuration descriptors. It claims no interfaces and installs no storage, MTP, ADB or audio class driver. Phone charging can still affect power demand.
- This diagnostic boot returns before NVS, Wi-Fi, display, audio and translation startup. Startup acceptance and actual USB readiness have distinct logs. Hotplug errors do not abort; close/initialization cleanup retries are bounded, and failed teardown retains resources until reboot. Managed and pinned vendor inputs remain unmodified.
- Focused tests passed 19 scenarios/1,135 checks on Clang and GCC13 UBSan; inherited CDC tests passed 23 scenarios/1,578 checks. The source format/SPDX workflow and shell syntax checks completed successfully. These are software checks, not phone or board acceptance.
- The initial Chinese diagnostic build and size check passed using ESP-IDF v6.1 preview in `build/korvo-s31` and its sdkconfig. Application: 239,328 bytes, SHA-256 `189f31a22484b08cda40ea2e6988c93314ba49605da757c28f8f7799390662b9`. RISC-V, local AOSL, pinned RTSA, video-off and four flash segments were checked. USB control transfer size is 1,024 bytes; existing Chinese, AEC, disabled AI QoS and 60 ms settings are preserved although business startup is skipped.
- The user then requested rapid diagnostic iterations without broad validation. Additional English/repeated-Chinese builds were interrupted; they are not claimed as passing. The earlier successful Chinese application and its ELF remain the test artifacts, and the saved configuration is restored to Chinese Host mode. The merged image and adjacent metadata were regenerated from the existing flash segments without compiling again. Direct esptool flashing in [the test instructions](USB_HOST_PROBE.zh-CN.md) avoids a rebuild and preserves NVS.
- Fourteen CDC diag=2 firmware/configuration files were backed up under `build/korvo-s31/before-usb-host-probe/` with recorded hashes; the original tested translation backup remains under `before-usb-probe/`. No device was flashed and no phone enumeration result is available yet. This Host comparison cannot establish Mac-to-S31 Device or USB audio operation.
- The user subsequently flashed the simple Host probe and supplied a successful Android-phone trace matching ELF hash prefix `0c5426da9`. The Host became ready at 872 ms, opened address 1 at 6,662 ms, reported a valid VID/PID and `speed=high`, and completed device/configuration descriptor reporting at 6,749 ms without a reported error. The configuration exposes one `ff/42/01` interface with 512-byte Bulk IN/OUT endpoints. This confirms real High-Speed Host enumeration/control traffic through J2 and the tested connection; it does not measure Bulk throughput, sustained power margin, Device mode or USB audio. The descriptor's 500 mA value is declared power demand, not a current measurement. No additional firmware build or broad test run was performed for this log assessment.

## 2026-11-04 isolated CDC Device comparison

- At the user's request for the next quick test, CDC startup now logs `event=usb_probe_boot scope=cdc_only` and returns before NVS, network, display, audio and translation initialization. The existing TinyUSB/PHY, descriptors, task configuration and diag=2 event counters are unchanged, so the comparison isolates application concurrency. Successful Host enumeration does not resolve the Mac Host role or J2's fixed VBUS wiring.
- The passed Host test segments, configuration and hashes are preserved in `build/korvo-s31/usb-host-probe-passed/`. The official kconfserver selected CDC and disabled Host in the existing Chinese sdkconfig. One direct ESP-IDF v6.1 preview build passed in `build/korvo-s31`; no extra language build or full test suite was run. The changed C file passed formatting and the working tree passed whitespace checks. Existing-image checks confirmed the new boot marker, RISC-V/local-AOSL/pinned-RTSA/no-video and four flash segments; merged delivery metadata was refreshed without another compilation.
- Application: 231,920 bytes, SHA-256 `a70b3a6e449f8302e10af0f1bdc491d7573d33d142fc182371d0296ac0b2e268`; ELF SHA-256 prefix `adb332c93`. Merged image: 362,992 bytes, SHA-256 `023fa9b1134b4b2b0fe65afd39838438ecfc07f3e2d4a0d4c3fafaa5c024cdd1`. Direct esptool segmented flashing avoids recompilation and preserves NVS. No board was flashed by the agent; isolated Device enumeration remains pending.
- The user subsequently supplied verified segmented flashing and an approximately 56-second trace of this exact ELF with `scope=cdc_only`. The probe continues logging; Device mode, clocks, soft-connect and USB_OTGHS interrupt allocation are present. Reported line state changes from 1 to 3 around 26 seconds, while address/frame, configuration state and TinyUSB reset/SETUP/transfer counts remain zero. Read-only Mac enumeration and the identity checker also find no native CDC probe. The failure therefore reproduces without application business startup; TinyUSB Device correctness and raw physical-reset reception remain unproven. Further comparison needs a confirmed Mac Host connection with J2's fixed VBUS output appropriately isolated. No additional firmware change or build was made for this assessment.

## 2026-11-05 Mosaico V1.2 minimum USB audio test

- The user confirmed a separate ESP-Mosaico CoreBoard V1.2. Read-only macOS inspection found its existing `mosaico` device at USB High-Speed, VID 303a/PID 4011, with UAC2 and a two-channel 48 kHz output device; no Mosaico input device was exposed. No playback or recording was performed during that inspection.
- Added the isolated `MYBOT_MOSAICO_UAC_TEST` build selector and `diagnostics/mosaico_uac`, using IDF 6.1, the immutable codec 1.5.9 ES8311 driver, TinyUSB 0.19.0~3 and official UAC 1.3.1. It is not a full MyBot board port. The app initializes V1.2 power/I2C and full-duplex I2S at 48 kHz/S16/stereo. Mono microphone samples are duplicated into USB stereo; USB stereo playback is mixed to the single speaker. No display, Wi-Fi, camera, NVS, filesystem, OTA or NAND application path runs.
- USB uses S31's UTMI High-Speed port 0 with one TinyUSB instance. A project-only forced include corrects ISR notifications/critical sections in the pinned UAC component and substitutes nominal sample-rate feedback. A four-block playback queue decouples I2S writes from USB callbacks. Software mute/volume and bounded error/drop counters support the short test. Long-term clock compensation and lossless operation are not claimed; the upstream single-slot handoff remains a limitation.
- The minimal build passed after adapting the codec API to the pinned version. Output remains in `build/korvo-s31`: `mosaico_uac_test.bin`, 272,576 bytes, SHA-256 `c73efdeadb6b763318500c72588711fc3b84461610ebec43e1d28b25892d8fbe`. RISC-V ELF and three flash segments at 0x2000/0x8000/0x10000 were checked. The whole application partition ends at 1 MiB; there are no data partitions. Changed-source formatting, whitespace and the flash helper's image-only checks passed. No broad host suite or extra language build was run.
- The prior Korvo CDC build/config remains under `build/korvo-s31/before-mosaico-uac/`. The new flash helper requires manual native-ROM mode, checks the test identity/write range, backs up the first 1 MiB of original NOR before writing, and saves an exact restore command. That prefix is sufficient only because this diagnostic has no writes outside its covered window; it is not a full-device/NAND backup. The helper has not opened or flashed hardware in this update. Mac input/output enumeration and actual simultaneous recording/playback remain pending. See [operation and rollback](../diagnostics/mosaico_uac/README.zh-CN.md).
- The user chose direct `idf.py flash` and subsequently reported not finding the expected sound-device name. Read-only macOS inspection now finds USB product `Mosaico UAC Test`, and CoreAudio exposes `usb uac` (manufacturer MyBot) with two input and two output channels at 48 kHz. That device is already the default output; the default input remains the built-in MacBook microphone. The generic name comes from the pinned UAC interface string, so instructions now distinguish it from the USB product name. Bidirectional audio-device enumeration has passed; actual playback, microphone capture and simultaneous audio have not yet been reported. No firmware rebuild or audio recording/playback was performed for this diagnosis.
- The user subsequently confirmed microphone capture in a voice-call app, but no speaker sound from either that app or local music/video. Mac inspection reports `usb uac` as the default output, output volume 100 and mute false; its streaming interfaces are open. This does not establish the device's received PCM level. The codec's initial volume 40 maps to -30 dB, while review found no definite PA/pin/BOTH-mode initialization error.
- Found a concrete TinyUSB 0.19 receive defect: the linear audio OUT ISR returns before rearming when a zero-length packet makes `tu_fifo_write_n` return zero. The diagnostic now selects the supported DWC2 PIO FIFO path (ring=1, DMA=0, slave=1), which rearms before notifying the application. A small temporary harness using the actual preprocessed RX function passed 0/24/192-byte rearm cases and failed-rearm handling. It proves the selected control flow, not that this event caused the hardware symptom.
- Added two short local startup tones before USB initialization, raised codec volume to 60 (-20 dB), and included USB mute/gain in UART status. A heard startup tone will distinguish the local codec/I2S/PA path from host playback. Incremental IDF 6.1 build and changed-source format/whitespace checks passed; no broad suite was run. The new app in the same directory is 276,864 bytes, SHA-256 `36e5408afe001808980d27d41476114e5b441c6af839eea7aef2a58bb13bc487`. Previous app retained as `mosaico_uac_test-before-speaker-fix.bin`. The agent did not flash or play/record audio; speaker recovery still requires the user's retest.
- The user heard the local startup tones, but reported very low volume and continued silence during music playback. This confirms audible local codec/I2S/PA output without establishing the USB playback path. At the user's request the codec volume is now 100 (nominal 0 dB); host mute and software volume remain effective.
- Added a read-only endpoint-zero vendor status request, without another interface: it reports actual OUT packet/byte counts before upstream buffering, callback/raw PCM peak, queued/written audio, written peak, errors/drops and mute/gain. `read_status.py` uses the Mac's installed libusb without interface claim, detach, reset or configuration changes. A temporary harness using the actual callback passed request rejection, DATA/ACK buffer-lifetime and maximum-counter JSON checks (421 bytes, below the 512-byte bound) with Clang/UBSan. No device status query was attempted against the older firmware.
- Incremental IDF 6.1 build and changed-source format/whitespace checks passed. Current app: 278,000 bytes, SHA-256 `c6b1a0deb75f5b173b793c01de7788c43298dc8f60c5c4d32ec42b75507a3329`, at the same `build/korvo-s31/mosaico_uac_test.bin`. This update adds observability; the music-silence root cause is not yet established. No automatic flashing, playback, recording or full regression run was performed.
- The user supplied two status samples with OUT fixed at 512 packets/12,288 bytes, callback bytes 12,264 and I2S-written bytes 11,520, while local capture grew. A read-only follow-up query reproduced the stalled OUT count. This is 64 ms of HS 48 kHz stereo S16 audio, with no write/drop errors and mute false; gain 10,362 represents about -10 dB software attenuation. Local capture is not evidence of successful USB IN completion.
- Rechecked the user's V1.2 guide, schematic pages 4/5 and official BSP: power, PA, I2C and I2S assignments match. In particular Codec ASDOUT is MCU RX40 and Codec DSDIN is MCU TX52; the schematic's codec-view net names must not be used to reverse the MCU configuration. UTMI port0 and HUSB320 hardware DRP control are correct. No board wiring or audio pin change was justified.
- S31's own `usb_otghs_struct.h` documents periodic IN `DIEPTSIZn.MC=01` for one packet per microframe. The pinned DWC2 driver left MC zero in normal scheduling and ISO IN retries. A diagnostic-only CMake adapter now checks the immutable input hash and compiles a generated copy setting MC=1 in both places, with at least one retry packet for ZLP. A temporary harness extracting actual functions passed 36 normal/retry/frame-parity cases; OUT and non-ISO register results were unchanged. Physical causality still requires a retest.
- Added a narrow transfer wrapper to supply correct nominal HS feedback from the first packet, before the upstream late parameter callback, and to count real feedback/MIC USB completion events. Its temporary Clang/UBSan checks passed payload correction, unchanged other/FS transfers, result propagation and completion/error accounting. The final link map includes the generated driver and both wrappers. Status payload capacity is now 1,024 bytes. No managed input was hand-edited.
- Incremental IDF 6.1 build and changed-source formatting/whitespace passed. Current app is 278,736 bytes, SHA-256 `95d2e5680a0079b93485b7656672bf1ce9a9929c4c903dd0ee1605afae4b41e2`; previous app is `mosaico_uac_test-before-iso-in-fix.bin`. No device flashing or audio playback/recording was performed by the agent, and continued playback after this correction is pending the user's hardware test.
- Subsequent user status shows every feedback completion failing (`fb_bytes=0`, `fb_completed=fb_errors`), while actual USB microphone bytes grow from roughly 5.6 to 6.2 MB with only 36 fixed startup errors. OUT remains at 512 packets/12,288 bytes. A read-only query reproduced that behavior over a longer interval. This establishes a feedback-endpoint failure separately from working microphone IN traffic.
- Identified another target-documented DWC2 configuration defect: S31 `DIEPTXF1_INEPNTXFDEP` requires at least 16 32-bit words, but the pinned allocator gives the four-byte feedback endpoint only one word. EP0 receives 16 words and the microphone receives 49, matching the affected-endpoint distinction. The generated diagnostic driver now clamps IN FIFO depth after optional doubling and before its capacity check; feedback descriptors and four-byte transfer size stay the same. Nine temporary host cases using the actual generated allocation function passed normal/half-empty sizes, unchanged OUT handling, and 15-word rejection/16-word acceptance. The original managed-source hash remains unchanged.
- Incremental build and whitespace checks passed in the same directory. Current application: 278,752 bytes, SHA-256 `eea9fda589ddab6744ef0eb687a21f565f67cd55b8900601e4bc56285401d5c1`; prior image retained as `mosaico_uac_test-before-fifo-min.bin`. No device flashing or audio playback/recording was performed by the agent. Hardware confirmation should show feedback success bytes increasing, feedback errors no longer accumulating rapidly, and OUT continuing beyond its initial 512 packets.
- The next user test still reports zero successful feedback bytes with every feedback completion failed; OUT is now fixed at 1,023 packets/24,552 bytes, while microphone USB bytes continue increasing. The FIFO correction therefore did not resolve the observed playback failure. These errors are DWC2 incomplete-ISO timeouts, which also occur if the host does not poll; they do not prove a packet was sent and rejected by the host.
- Compared the pinned component's HS feedback `bInterval=1` (125 us) with the same-version TinyUSB canonical `uac2_speaker_fb` example, which uses HS 4 (1 ms) / FS 1. Historical public AppleUSBAudio UAC2 parsing converts feedback interval using `bInterval-1-3`; values below 4 can underflow. The current Mac uses newer `usbaudiod`, so this is evidence for a compatibility test, not proof of its internal behavior or a USB specification violation.
- Added a project-local configuration-descriptor wrapper selecting HS feedback interval 4 and FS interval 1, including the opposite-speed descriptor. The remaining 254-byte descriptor is unchanged except the feedback interval and other-speed descriptor type. Temporary tests using the actual ELF descriptor and actual helper functions passed HS/FS/current/other-speed mapping, unchanged endpoint addresses/sizes and malformed-descriptor rejection with Clang UBSan. The link map confirms the USBD configuration request calls the wrapper. No additional controller parameters were changed in this iteration.
- Incremental IDF 6.1 build and changed-source format/whitespace checks passed. Current app: 279,056 bytes, SHA-256 `c4127a53ef9b1817c0322dc51a48a4108380df3ed9a70938cdca67b637277959`, in `build/korvo-s31/mosaico_uac_test.bin`; prior image retained as `mosaico_uac_test-before-feedback-interval.bin`. USB status now includes `fb_interval`. Hardware confirmation remains pending; the agent did not flash or play/record audio.
- The user subsequently confirmed audible device playback after this descriptor change. Three status samples show OUT increasing from 503,582 to 532,265 packets, I2S-written playback from 12,085,440 to 12,773,760 bytes, and actual USB microphone bytes from 12,096,764 to 12,785,180. Feedback success bytes grow from 251,796 to 266,140; feedback errors stay at 54 and microphone errors stay at 77. Playback/capture I/O errors, OUT errors, feedback-submit errors and playback queue drops remain zero. These observations establish successful short-run bidirectional USB audio on this Mac/Mosaico V1.2 connection and associate playback recovery with the HS feedback-interval change to 4. They do not prove the current macOS daemon uses the historical parser or establish long-term drift/quality/AEC behavior.
- Codec volume is 100, mute is false, and USB gain remains 10,362 Q15 (about -10 dB); the effective host software attenuation is separate from codec maximum volume. The user-tested image and its flash segments/configuration are preserved, with hashes, in `build/korvo-s31/mosaico-uac-passed/`. No additional firmware changes or rebuild were made for this acceptance record.

## 2026-11-05 Mosaico translation migration

- The user changed the active objective from meeting integration to porting the two existing
  translation modes to Mosaico V1.2. It is now the default `MYBOT_BOARD` and future design target.
  Korvo remains an explicit profile. No meeting audio routing was added.
- Added ES8311 standard stereo I2S at 16 kHz/MCLK256 with SDK mono conversion, shared audio
  lifecycle, AI short/long press controls, CO5300 QSPI/CST9220 and a 480×480 LVGL layout.
  Provisioning, binding, captions, language selection and both existing cloud modes are reused.
- The passed standalone 48 kHz UAC image remains preserved in
  `build/korvo-s31/mosaico-uac-passed/`, including its bootloader, partition table, sdkconfig and
  flash arguments. Its279056-byte app SHA-256 is
  `c4127a53ef9b1817c0322dc51a48a4108380df3ed9a70938cdca67b637277959`, rechecked after the build.
- Direct `idf.py --preview -B build/korvo-s31 ... build` passed for Chinese on ESP-IDF 6.1.
  The build uses `MYBOT_BOARD=esp-mosaico-v1.2`, `MYBOT_MOSAICO_UAC_TEST=OFF`, ES8311, Octal
  PSRAM, 60 ms and the normal NVS/OTA partition layout. AI QoS, video and USB diagnostic boots
  are off. Runtime USB is a CDC log console; the UAC component is absent.
- Application: `mybot_esp32s31.bin`, 3581664 bytes (0x36a6e0), SHA-256
  `a5712bff3e9dda3b3333d17dac29d79e9feee22ac5e0e529f0cb28e99dd61c01`.
  The 0x3f0000 application partition has 13% space remaining. Four flash segments were verified;
  ELF is RISC-V, RTSA inputs match the approved hashes, AOSL symbols come from the local
  adapter, and exactly one MyBot/AOSL/RTSA component is present.
- Host audio fault/lifecycle and input suites passed for both board backends. Mosaico display
  adapter 18 lifecycle/failure cases passed normally and with UBSan. The shared UI suite passed
  at 480×480 and 800×480, both UI languages and both input selectors. Geometry checks cover
  containment, 44×44 minimum touch targets, overlap, language submission gating, captions and
  scene transitions. A read-only audit measured 250 unique labels against the actual fonts
  without finding static text-height overflow.
- Builder/delivery tests: 55 cases on IDF Python 3.10, 50 passed and 5 Python 3.11+ safe-cleanup
  cases skipped. Changed/new C/C++ files passed clang-format 18 and SPDX checks; whitespace
  checks passed. CI now targets Mosaico language variants; no remote CI run is claimed.
- Display transfer timeout stops further submissions and retains in-flight DMA storage; this
  first port requires a device reboot to recover the display. ASan could not run under the
  local toolchain (Illegal instruction before test diagnostics); it is not counted as passing.

Hardware acceptance for this migration is pending: panel direction/colors, physical touch,
16 kHz translation audio, Cloud AEC, Type-C CDC and repeated sessions need user flashing.
The previously reported UAC microphone/speaker success is a separate hardware baseline.
The build was not flashed by the agent, and no commit or push was made.

## 2026-11-05 Mosaico touch and rounded display

The user supplied a physical-device photo showing the Home page, connected Wi-Fi, clock
and volume, with status text clipped by the screen corners, and reported that tapping the
translation entry did nothing. This confirms that the new image boots and renders on this
board; it does not establish working touch or translation audio.

A read-only 15-second capture from the connected CDC console received repeated
`cst9220: Malformed report header: first=0xab marker=0xab` approximately every 30 ms.
The adapter had read an already-acknowledged HYN212 report on each LVGL poll, and each
invalid report cancelled gesture recognition before a valid release could arm it.

- Touch now uses active-low INT6 with an IRQ readiness latch and level fallback. No new
  report returns the last contact sample; holding a finger does not synthesize release.
  Errors cancel the gesture and remain invalid until a new valid report. Press/release
  coordinates and out-of-range coordinates are logged for hardware verification.
- GPIO readiness is sampled before the atomic pending exchange so an IRQ during level
  sampling cannot trigger a duplicate read of the same acknowledged report.
- Mosaico status text is inset 60 px, body controls 32 px, bottom controls avoid the rounded
  corners, and the Home entry is single-line with less unused vertical space.
- Existing UI tests pass with rounded 64 px-corner bounds, button sizing and overlap checks.
  A temporary smoke test linked the production LVGL, view and fonts: a pointer press/release
  at the actual Home button centre successfully entered scene selection.
- Display 18-scenario lifecycle tests and new actual touch-adapter readiness/error/bounds/
  teardown tests pass normally and with UBSan. Formatting, SPDX and whitespace checks pass.
- Incremental Chinese build at `build/korvo-s31` passes. App size 3582224 bytes (0x36a910),
  SHA-256 `4ff79d20eb74ca0d8cec8a8b5f320e638dca97511004d81e3f8bf58178e07d22`.
  RISC-V/local-AOSL/RTSA fingerprints and all four flash segments pass the build checker.

The new touch/corner fixes still require user flashing. No coordinate rotation or axis swap
was guessed from the photo; the packet-read failure was directly observed on the device.

## 2026-11-05 Mosaico touch Y direction

The user confirmed the previous fix was flashed (single-line Home entry visible), but taps
still did not operate the entry or volume. During their requested click/drag sequence, a
read-only 45-second capture from `/dev/cu.usbmodemMOSAICO_V121` collected 41 press/release
pairs, with zero malformed reports or adapter read errors. The prior readiness fix now
produces valid gestures; it did not address the separate physical Y orientation.

- Home occupies screen Y226–321. Reported taps Y174–215 map to Y264–305 under `479-y`.
- Volume occupies screen Y376–419. Reported drag endpoints Y69–89 map to Y410–390.
- Both independent controls support a Y mirror, with no X swap/rotation. Enabled the public
  touch config `mirror_y` on the Mosaico adapter; the managed driver remains unchanged.
- The actual common touch getter passes recorded-point/edge transforms with UBSan. Mapped
  point (224,281) injected into actual LVGL/view/fonts enters the scene selector. Existing
  18 display lifecycle scenarios and updated touch tests pass normally and with UBSan.
- Chinese incremental build and final RISC-V/local-AOSL/RTSA/four-segment checks pass in
  `build/korvo-s31`. Application size 3582224 bytes, SHA-256
  `541a9b3d29427c99b1ab5f68cb5c1a06aa550cfcf36bccdb4f5259cd3cf8ca9c`.

The agent read CDC without sending serial data, resetting or flashing; DTR/RTS were false.
The runtime log port differs from the ROM download port. The new Y mapping remains pending
user flashing/confirmation; this record does not claim completed physical interaction.

## 2026-11-05 Mosaico session persistence diagnostics

The user's device log confirms touch navigation and the media start action now reach the
service. The service returned HTTP 201 and a valid Chinese-to-English listen route, then the
local known-session journal save failed. The device correctly aborted local startup, ended
the remote session with HTTP 200, and erased the pending journal. RTC and audio startup were
not reached. The runtime latches STORAGE_ERROR until restart.

The initial UNKNOWN journal was saved successfully before HTTP dispatch. The failing overwrite
uses the same `tr_session` key and 1456-byte record. A static key/blob-length limit cannot
explain this sequence. Capacity/fragmentation, flash write errors or allocation pressure remain
unconfirmed because the old platform adapter discarded the NVS error code. No NVS erase,
partition migration or journal-safety bypass was introduced.

- KV failures now log exact API operation, key, byte count, error name and numeric code.
  NVS used/free/available entries and internal/PSRAM free/largest-block figures are emitted
  at open and on errors. Successful committed writes log key/size only. Stored values are
  never included by this diagnostic.
- Journal allocation failure is distinguished from platform save failure and identifies
  UNKNOWN/KNOWN phase. The previous abort/cleanup behavior is preserved.
- New platform KV fault tests pass normally and with UBSan, checking error propagation,
  commit ordering, optional statistics failure, no write-failure erase/retry and no payload
  logging. Existing 31 translation lifecycle fault scenarios pass with UBSan. Format/SPDX
  and whitespace checks pass.
- Chinese incremental build at `build/korvo-s31` passes: 3584416-byte application, SHA-256
  `b17effa1a7c01583d157ce93547eb171e48bdc77bb482c40abf3b6d16ea4e814`.
  RISC-V/local-AOSL/RTSA and four flash segments pass the final build checker.

This is a diagnostic build; the underlying device storage failure has not yet been fixed
or attributed to a specific NVS/heap error. User flashing and one further start attempt
are required to collect that evidence.

## 2026-11-05 Compact session journal

The new user log establishes the storage failure: `nvs_set_blob(tr_session,1456)` returned
`ESP_ERR_NVS_NOT_ENOUGH_SPACE (0x1105)`. At boot NVS had 311 used / 67 available entries;
the UNKNOWN record succeeded and left 360 used / 18 available. The KNOWN overwrite failed
twice, while internal free heap was about 190 KB and PSRAM about 16 MB. Cloud cleanup
returned HTTP 200 and cleared the journal. The 16 KiB shared NVS and live retained records
left too little space for both fixed-size journal copies during an atomic overwrite.

- New journal writes use version 2: a 16-byte header (version, phase, expiry), followed by
  five bounded NUL-terminated strings (server, device, credential, parent ID, stop reason).
  Only actual bytes are stored. Authentication/language records keep their prior format.
- Version 1 fixed-size records remain readable for cleanup after upgrade. Both formats
  validate all fields before publishing the recovered identity; malformed/truncated/trailing
  data is retained and blocks a new cloud start. Pre-dispatch persistence and post-start
  cleanup ordering are preserved. No NVS/partition erase or migration is required.
- The conservative copy-on-write entry model reproduces 49+49>67 for the old format and
  verifies 23+23<=67 for 609/630-byte compact records using a 511-byte synthetic credential,
  deployment-shaped endpoint/ID lengths and repeated start/stop. This models entry pressure;
  it is not an NVS page/flash emulator.
- All 35 lifecycle/fault scenarios pass with UBSan, including legacy recovery, compact
  unknown/known roundtrips, malformed records and latched post-start write failure.
  Formatting, SPDX and `git diff --check` pass.
- Direct Chinese incremental build at `build/korvo-s31` and final RISC-V/local-AOSL/RTSA/
  four-flash-segment checks pass. Application 3584784 bytes, SHA-256
  `061e4e8af394c49a7ab114243e30c4526c6861a259f2b60c25556a6ac1d33ac7`.

The compact format awaits user flashing and a successful hardware media start. Existing
Wi-Fi, binding, language and volume data remain in their existing NVS locations.

## 2026-11-05 Caption stripe comparison

The user supplied a face-to-face screenshot with finalized English/Chinese captions and
reported a light horizontal stripe through the English glyphs. It remains after at least
five seconds without speech or interaction, so transient scan tearing alone does not
explain the report. The exact device failure is not yet identified.

Read-only checks reproduced the same text with production LVGL/fonts at 480×480: 8-row,
40-row and full-frame software renders matched; long-to-short dirty updates and forced
repaints differed by zero caption pixels. The real custom LVGL allocator with poisoned
new/free memory also matched. UI has no line decoration; the IDF SPI path already performs
TX cache synchronization. These checks cannot simulate S31 memory hardware or panel writes.

A controlled hardware comparison changes only partial-buffer capacity from 8 to 40 full-width
rows. SPI clock, panel initialization, TE setting, coordinates, font and translation remain
the same. Internal DMA staging grows from 7680 to 38400 bytes; LVGL's separate PSRAM buffer
grows equally. The initialization log now prints the actual row count. The previous app
and hash are saved in `build/korvo-s31/display-partial8-baseline/`.

- Display 18 lifecycle/fault scenarios plus touch tests pass with UBSan; flush checks cover
  the whole staging payload. Format/SPDX/whitespace checks pass.
- Direct Chinese build and final RISC-V/local-AOSL/RTSA/four-segment checks pass in the fixed
  build directory. Application 3584800 bytes, SHA-256
  `96d847876512f230087370668124bb6f2fd420b2b2d9323e34cbfffd9f7f358b`.

The user subsequently flashed this version and confirmed that the horizontal stripe is gone
and the display is normal. Retain 40 rows as the current Mosaico display baseline. This is
user-reported hardware confirmation of the visible fix; it does not identify the exact
controller-level cause or establish long-duration/audio performance.

## 2026-11-05 Mosaico composite USB transport

Meeting integration step 1 adds UAC2 and CDC to the existing Mosaico translation image.
It is transport-only: PC playback is received/discarded and USB microphone packets contain
silence. Existing media/face-to-face audio still uses the codec; no meeting routing or
resampling was added. One esp_tinyusb instance owns the PHY/task/descriptors; UAC is
prepared first with AS_PART/skip_tinyusb_init, including internal-RAM ISR buffers.

- Audio 48 kHz/S16/stereo, interfaces 0–2, endpoints OUT01/FB81/MIC82; CDC interfaces 3–4,
  endpoints 83/04/84. HS feedback interval 4 and FS 1, CDC bulk 512/64. Composite descriptor
  strings name the audio function MyBot Mosaico Audio.
- Production reuses the immutable-input-checked DWC2 generated compilation copy, scoped
  ISR compatibility and PIO/FIFO audio path. The duplex alternate-interface array is
  explicitly sized for two streaming interfaces. Nominal feedback remains fixed for
  this transport check; real audio-clock compensation belongs to the later routing step.
- Every five seconds, CDC logs report actual successful USB OUT/MIC/feedback bytes and
  errors separately from application callback/discard/silence counters. No raw PCM is
  logged or routed to the device microphone/speaker.
- Actual pinned-header descriptor tests pass with UBSan. Transport callback tests pass
  with UBSan, covering lengths, silence, empty packets, state, failed initialization,
  feedback, endpoint statistics and unchanged CDC forwarding. Format/SPDX/diff checks pass.
- Direct Chinese IDF 6.1 build passed in `build/korvo-s31` using its own sdkconfig. The
  root project now defaults sdkconfig to the selected build directory if unspecified.
  Application 3595424 bytes, SHA-256
  `4b111b3817972bd94b37804c10d597da92c9c256a6a9eb7e25a873f8a954be2f`.
- RISC-V/local-AOSL/RTSA/four-flash-segment checks pass. The build contains one each of
  TinyUSB, esp_tinyusb, UAC and the existing runtime components. Link ownership confirms
  descriptors/mount callbacks in esp_tinyusb and feedback override in the platform.

Mac enumeration, simultaneous CDC/UAC traffic and coexistence with live translations
remain hardware tests for this new composite image. The standalone UAC result does not
prove composite enumeration. Flashing and the expected silent-audio behavior are described
in [the step 1 guide](MOSAICO_USB_AUDIO.zh-CN.md). No device was flashed or commit created.

## 2026-11-06 Composite USB link interposition

The user confirmed MyBot Mosaico Audio appears on macOS, and CDC logs work through
`/dev/cu.usbmodemMOSAICO_V124`. A continuous-playback log spans 40 seconds with
7,587,264 additional callback-discarded bytes and 7,680,000 additional prepared-silence
bytes, and no application callback errors. Wi-Fi binding polling continues concurrently.
However, all actual USB transfer counters remained zero; those zero error counters could
not establish error-free bus traffic.

The final ELF revealed an integration defect: the post-project USB helper appended to
IDF's LINK_OPTIONS build property after it had been copied to the executable. Neither
GNU --wrap option reached the actual link, so both wrappers were discarded. This also
disabled the first high-speed feedback correction, not just the transfer counters.

- Apply both wrap options directly with target_link_options on the final executable.
- The build checker now checks the application's Ninja link flags and nonzero wrapper
  addresses in the live map region, excluding discarded input and cross-reference names.
- 20 delivery tests pass, including missing flags, unrelated-target flags, discarded-only
  names and missing/zero wrapper symbols. Actual firmware check passes; nm shows both
  wrappers and disassembly confirms calls from USB code reach them.
- Chinese build passed in `build/korvo-s31`: 3595792-byte application, SHA-256
  `beafe1ce2e4cf623cb82796426588cde22085aec40cb8f5e28625b18ec04e737`.
  The existing S31 ABI/AOSL/RTSA/flash checks pass.

The user's subsequent log confirms that the corrected image records actual transfer
completions while CDC logs and Wi-Fi/HTTPS polling continue. Both audio streaming
interfaces open; the device stays mounted at high speed without a detach or suspend.
The user also reports switching the Mac audio selection to the built-in MacBook device
during this capture. Counters pause between 21.614 and 26.614 s and streaming interfaces
open again before 31.614 s, consistent with a host stop/reopen rather than a USB hang.
Exact switch timestamps are unavailable; this is not a controlled uninterrupted-stream
test. In the 31.614–51.614 s comparison interval:

- Successful USB OUT bytes increase by 3,839,952 and microphone IN bytes by 3,840,000.
  Both match the nominal 192,000 bytes/s for 48 kHz, stereo S16 audio within sampling
  granularity. OUT errors remain zero; MIC errors remain at 78 in this interval.
- Feedback has 20,000 completions and 79,940 successful bytes. Errors increase from
  56 to 71: 15 unsuccessful completions (0.075%). Feedback stays at 393216 and submit
  errors remain zero. The pinned DWC2 driver reports FAILED after exhausting incomplete
  ISO IN retries; TinyUSB then submits another feedback transfer. This explains continued
  traffic despite those errors, but does not establish their timing/hardware cause.
- Discard callbacks account for 3,790,464 bytes, 49,488 fewer than successful OUT bytes
  (about 1.29%). The upstream UAC ISR and worker share one buffer/length and merge task
  notifications; this path cannot establish lossless delivery to a future audio consumer.
  Prepared silence increases by 3,840,000 bytes; application callback errors remain zero.

This establishes short-duration USB duplex transport alongside CDC and network activity,
not error-free endurance or live translation coexistence. Feedback errors grow in the
sampled interval, but a capture with fixed input/output selection is needed to distinguish
stream-switch effects from steady-state failures. The callback path needs reliable buffer
ownership before carrying meeting audio. The final
56.614 s status line is truncated and is not used for the complete-sample comparison.
USB audio still discards playback and returns silence; no physical or RTC meeting routing
was tested. No device was flashed or commit created by the agent.


## 2026-11-06 Meeting listening

The user selected PC audio → LISTEN Agent → device speaker as the next integration step.
Mosaico's meeting page now selects LISTEN with USB capture, initially remote English to
local Chinese. It reuses the existing speaker, caption, pause and cleanup paths. USB
microphone return still contains silence; simultaneous meeting SPEAK is not implemented.

- Optional platform USB capture uses the existing 16 kHz mono S16 frame-count contract.
  Mode, source and optional language pair are committed together only while idle. USB
  playback must be mounted, unsuspended and open before any cloud start is issued.
- Source changes stop and join the capture worker before destroying its device; failed
  teardown retains ownership. Session end, rejected/unknown starts and pre-dispatch
  failures restore codec capture. Recovery failure blocks the next cloud start.
- Digital USB input sends zero AEC reference in the existing 60 ms paired format. Playback
  neither fills nor waits on the physical-reference ring for this input source.
- A 30,720-byte internal USB receive ring replaces upstream's shared speaker buffer.
  A 95-tap Q15 low-pass FIR converts 48 kHz stereo to 16 kHz mono on the capture worker.
  Normal residual is 20 ms; read-entry backlog above 120 ms is trimmed to 80 ms before
  the 60 ms read. The hard limit is 160 ms. Bounded occupancy feedback corrects clock
  mismatch; underflow is explicit silence. Stop/stream close/unplug/suspend invalidate
  queued and already-copied PCM. Master and channel mute/dB gain affect the digital input;
  the log reports master controls separately from physical speaker volume.
- Focused USB UBSan tests pass for queue cadence, generation invalidation under concurrent
  stop/close, overflow recovery, feedback, channel controls and malformed requests. FIR
  tests measure 1 kHz gain 0.99997, 8.5 kHz residual 0.000289 and 12 kHz residual 0.000100;
  arbitrary input chunking produces identical output. These are host numerical checks.
- Control/lifecycle/media switching tests pass, including 36 lifecycle scenarios and
  13 media pause/source tests. Control coverage includes AEC on/off, source admission and
  failed-start cleanup; state/volume/input regressions pass. UI checks pass in both
  languages at 480×480 and 800×480. The static UI font was regenerated from the pinned
  input for the new computer-audio labels. Descriptor, format/SPDX and whitespace checks pass.
- Direct Chinese IDF 6.1 build passes in `build/korvo-s31`; application is 3,634,992 bytes,
  SHA-256 `434f45998b13a9e66ea95aeb7beaf704968536d3b4c55dc49dd2b1b68a4f8e26`.
  Final checks confirm RISC-V, local AOSL, pinned RTSA, video disabled and four flash
  segments. The live map includes the capture/resampler and all platform UAC callbacks.
  The previous USB transport image is retained locally in `build/korvo-s31/usb-transport-baseline`.

This revision has not been flashed by the agent. Actual English-to-Chinese meeting audio,
caption correctness, host mute/volume, stream switching, clock feedback and endurance need
user testing with this image. Previous composite transport logs do not establish these
new behaviors. Instructions are in [the meeting USB guide](MOSAICO_USB_AUDIO.zh-CN.md).


## 2026-11-06 Meeting UI snapshot forwarding

The user reported that Meeting still displayed the unavailable preview after the listening
integration. Both board display adapters constructed a zero-initialized snapshot and copied
fields individually, omitting `usb_audio_available` and `audio_source`. The view therefore
always received no USB capability and the codec source, despite correct platform registration
and presenter state. The earlier UI tests injected snapshots directly and missed this boundary.

Both adapters now forward these two fields. Real-adapter host regression tests reproduce the
missing capability before the fix, then pass capability-only/source-only updates, queued
snapshot ownership and default-source restoration after the fix. The Mosaico 18 display
scenarios and touch checks, plus all Korvo display scenarios, pass with UBSan. Formatting,
SPDX and whitespace checks pass. The 40-row display buffers are unchanged.

Direct Chinese incremental build and the final S31 link/flash checks pass in
`build/korvo-s31`: 3,635,008-byte application, SHA-256
`cd8c5e790470eaa622e3de5be8413227ece6cb1f31b2c501046abf3128957c68`. No hardware was flashed by the agent; the user must flash this corrected image
to verify the Meeting entry and continue the PC-audio translation test.


## 2026-11-06 User confirmation of meeting listening

The user reports that the meeting listening test passes after starting PC audio playback
before pressing Start on the device. The preceding USB_AUDIO_NOT_READY log showed a mounted
USB device but no opened playback interface and zero received bytes; this was consistent
with the required preflight check. No firmware/source change or rebuild was made for that
report, so there is no corresponding diagnostic patch to revert. The earlier display
snapshot fix remains part of the working version.

The user subsequently explicitly confirmed that the device plays the translated Chinese
speech normally and supplied an active USB listening log. In the 196.591–231.591 s sample
interval, successful OUT bytes increase by 6,719,620 (about 192,000 bytes/s). Converted
capture frames increase by 559,680 (about 16,000 samples/s); sampled PCM peaks are nonzero.
Application queue drops remain zero and inserted-silence frames remain at 296, showing no
new queue underflow in that interval. Binding HTTPS polling continues concurrently.

Feedback failures increase from 112 to 119 over 34,999 completions and MIC failures from
41 to 42; this is not an error-free bus result. A later streaming-interface reopen is
followed by additional initial errors and silence padding, after which receive/capture
counts and nonzero peaks continue. The log excerpt contains transport measurements, while
the audible Chinese translation result is confirmed by the user.

This establishes functional PC-audio-to-Agent-to-device-speaker meeting listening on the
user's hardware. Quantitative audio quality and long-duration clock stability require
separate validation; microphone translation return to the PC is still unimplemented.


## 2026-11-06 Bidirectional USB meeting translation

After the user confirmed normal Chinese playback in meeting listening, the next implementation
adds the reverse path and runs both routes concurrently. Meeting selects `listen_speak` plus
USB audio, with my/local Chinese and other/remote English by default. One parent conversation,
two RTC connections and one RTM subscription retain the existing dual-route protocol.

- The physical pipeline sends MIC to SPEAK and plays LISTEN output on the codec. Its AEC
  reference is the physical speaker PCM. A separate PSRAM-allocated USB pipeline sends PC
  audio to LISTEN with zero reference and plays SPEAK output to the USB microphone sink.
  The USB pipeline does not own codec volume, announcements or an unused reference ring.
  The prior per-session capture-device switching helper is removed.
- Source/mode/language selection is atomic while idle, USB capability requires both operation
  tables, and capture preflight still requires PC playback to be open. The USB microphone
  may be opened later; translated PCM while it is closed is consumed/discarded, never replayed.
- Both paths share aggregate RTC readiness and pause/stop admission. An intentional pause
  interrupting USB I/O does not report a fatal device error. Meeting teardown quiesces both
  device queues even after RTC disconnect, then joins workers before freeing the USB object.
  RTC/worker failure retains ownership for a later retry; physical idle playback is restored.
- A task-side polyphase FIR interpolates 16 kHz mono to 48 kHz. The separate internal return
  ring holds 7,680 mono samples (15,360 bytes), with 20 ms priming and bounded rate correction.
  USB microphone packet payloads are 6 stereo frames at HS and 48 at FS, scheduled by host
  transfers; no 10 ms UAC task cadence drives this path. Silence fills unavailable PCM.
  A submitted packet is immutable until completion; stopping cannot retract that one packet.
- Interposition is at `usbd_edpt_xfer_fifo`, forwarding the microphone through `usbd_edpt_xfer`
  for normal endpoint busy bookkeeping. A local RISC-V linker experiment showed that wrapping
  `dcd_edpt_xfer_fifo` would not reliably intercept its same-object weak fallback. The final
  ELF contains live calls from `audiod_control_xfer_cb` and `audiod_xfer_isr` to
  `__wrap_usbd_edpt_xfer_fifo`. Managed inputs remain unchanged.
- Nine focused USB UBSan cases pass, including both interface lifetimes, concurrent stop/close/
  destroy, nominal FS/HS packets, short utterances, capacity handling and both filters.
  Simulated +417/-417 ppm clocks over 144 seconds produce no additional underflow/drop after
  priming. The interpolation numerical test gives 1 kHz gain 0.99998 and 15 kHz image residual
  0.000048; varied chunk boundaries match. These are host checks, not device timing/quality results.
- Media pipeline 14 cases and route/control 14 cases pass with cloud AEC both enabled/disabled;
  lifecycle 36, RTC 48, state 141 assertions, volume 8 and mailbox/UI/caption regressions pass.
  UI tests pass in both languages at 480×480/800×480, with simultaneous directions, correct
  caption labels, pause and source acknowledgement. Descriptor tests and 20 delivery checks
  pass, including the new live wrapper/link option. Formatting/SPDX and whitespace checks pass.
- Direct Chinese IDF 6.1 build passes in `build/korvo-s31`: application 3,653,536 bytes,
  SHA-256 `cf2abac3dc51ed1a07835804d4a176c05f59771d41e4df9e9cd8aa1544871104`.
  Final S31 RISC-V/local-AOSL/RTSA/video-off/four-segment checks pass. The symbol map places
  the input ring, return ring and 192-byte USB packet in internal memory. The previously
  user-confirmed listening image is retained at `build/korvo-s31/meeting-listen-passed/`.

This new duplex firmware has not been flashed by the agent. User acceptance must verify
English recording on the PC from the device's Chinese microphone, simultaneous opposite
traffic, pause/stop/restart, acoustic AEC and longer runs. The earlier listening acceptance
remains a separate baseline. Use the updated [meeting guide](MOSAICO_USB_AUDIO.zh-CN.md).


## 2026-11-06 RTC initialization and PSRAM task stacks

The user's duplex test opened both USB streams and obtained HTTP 201 with valid LISTEN/SPEAK
routes, local Chinese and remote English. Local `agora_rtc_init` then returned ERR_FAILED
before RTC join. Cloud stop succeeded with HTTP 200 and confirmed the parent ended; no
translated return PCM was generated. USB transport continued operating.

Read-only reference-source inspection and the pinned binary narrow this return to creation
of the first AgoraRtc MPQ. The inherited ESP32 AOSL HAL uses xTaskCreate: enabling external
stack support does not automatically move those task stacks out of internal RAM. Starting
the extra USB pipeline had added three 16 KiB internal stacks before RTC's own task allocation.
The log's 173,435 free / 75,776 largest internal block measurement preceded codec and media
startup; allocation-time heap figures were absent. Internal-memory pressure is therefore the
supported explanation, while the exact failed allocation must be confirmed by new logs.

- A project S31 adapter is force-included only into the original AOSL thread HAL compilation
  unit. Exact audited task names cap_mpq/pb_mpq/mybot_mpq, service_mpq and mybot_rtc use PSRAM
  stacks; control_mpq and opaque vendor RTC tasks retain internal stacks. Six media stacks
  plus HTTP and forwarding reduce internal-stack demand by about 120 KiB without shrinking
  stack sizes or altering priorities. Existing PSRAM data-buffer allocations remain in place.
- The deletion adapter inspects the actual stack address and pairs WithCaps creation with
  WithCaps deletion on both ordinary return and explicit thread exit. Other tasks retain
  their original allocation/deletion. Task logs report placement and, on failure, requested
  stack bytes plus internal/PSRAM free and largest blocks. TCBs and USB ISR buffers stay internal.
- Fixed RTSA sets its initialized flag before its MPQ creation and does not reliably roll back
  that flag after failure. The wrapper now latches a terminal init/fini failure until restart,
  retires its own callback queue and prevents a false-success subsequent vendor init.
- The new host CMake case compiles the unchanged upstream HAL using the production wrapper.
  UBSan tests cover exact-name policy, size/priority, normal return, explicit exit, renamed task
  deletion, unknown/truncated-name rejection and allocation failures. RTC 50 scenarios pass,
  including failed-init/forwarding-cleanup retry without another vendor init or fini call.
  Format/SPDX and whitespace checks pass; CI includes the new focused task test.
- Direct Chinese build passes in build/korvo-s31: application 3,654,352 bytes, SHA-256
  `25715672b5bf35cfd61930e375423d1c2ab08fbd51d42983896ae639ae8868e7`.
  RISC-V/local-AOSL/pinned-RTSA/video-off/four-segment checks pass. Final ELF inspection
  confirms the actual AOSL create and normal-return calls reach the S31 adapter; the production
  HAL object also confirms explicit thread exit uses the matching deletion adapter.

No device was flashed by the agent. The user subsequently reported that testing passed after
flashing the PSRAM stack revision. This records user-reported functional acceptance of the
current bidirectional meeting firmware; no additional device log or quantitative measurements
were supplied with that confirmation. Detailed repeated start/stop reclamation, acoustic AEC,
audio quality and endurance remain separate validation items.

The user requested PSRAM wherever practical; audited task stacks and large task buffers follow
that policy while required internal-memory objects retain their placement. The global external
BSS option remains disabled, avoiding an unrelated relocation of Wi-Fi/lwIP static storage.


## Review fix 01: isolate Mosaico UAC from Korvo

UAC's manifest now belongs to the nested mybot_mosaico_usb component, registered only
for Mosaico before IDF dependency discovery. The global main manifest no longer downloads
UAC for Korvo. CI builds both boards in both languages, with distinct output names and
Mosaico-only descriptor tests. The focused workflow/delivery suite passes 21 cases.

Fresh Korvo Chinese and restored Mosaico Chinese builds both pass in the same local
build/korvo-s31 directory, with final S31 ABI/AOSL/RTSA/flash checks. Korvo's compilation
graph has no UAC component/source; Mosaico contains the board wrapper and composite UAC,
without standalone UAC descriptors. The original Mosaico build was preserved and restored;
no device was flashed. Pinned-header descriptor tests pass. Remote CI remains pending.

- Korvo application: 3541936 bytes, SHA-256 `44438cde7b2b620cd6851756b87f02f5d94edd6f5ac1fd2357911581e413ea3f`.
- Mosaico application: 3654352 bytes, SHA-256 `25715672b5bf35cfd61930e375423d1c2ab08fbd51d42983896ae639ae8868e7`.


## Review fix 02: preserve NVS on startup errors

Storage startup now attempts NVS initialization once and returns on any error without
an automatic erase. Wi-Fi/auth data and pending cloud session journals remain available
for explicit recovery. A small startup helper is exercised with success, no-free-pages,
newer-format, allocation and generic failure mocks; all verify zero erase calls and
unchanged retained bytes under UBSan. CI runs this targeted test. Formatting/SPDX and
whitespace checks pass. Chinese incremental build and final S31 checks pass in the fixed
build directory: application 3654352 bytes, SHA-256 `0c8502571d9385677438ee42eabfbabce87568428d0f1dd57584326263db6527`.
No device was flashed; real corrupt-partition recovery is not emulated by the host test.

## Review fix 03: retain ambiguous gateway starts

An HTTP 5xx without a usable parent session now preserves the unknown-start journal unless
the server explicitly returns the boolean cleanup_required=false. A known parent still
requires cleanup. Partial transport failures follow the same rule; 4xx rejection semantics
are unchanged. Client UBSan tests pass 467/459 cases with features enabled/disabled,
including 502/503/504 malformed/empty responses. Lifecycle tests pass 36 groups, including
durable unknown results and blocked duplicate starts after reboot. Formatting and whitespace
checks pass. The Chinese incremental build and final S31 checks pass in build/korvo-s31.
No device was flashed; gateway failure injection remains a host result.

## Review fix 04: recover physical playback faults

The physical pipeline now reports and latches hard playback faults. Session cleanup
stop/starts the backend and only clears the latch after success; a failed recovery
blocks a new cloud start. The common error callback closes routing through cancellation
without dereferencing a concurrently retiring USB pipeline. Intentional pause/stop
interruptions do not latch faults. Media tests pass 17 cases with AEC on/off; control/UI
and face tests cover idle failures, cleanup and subsequent successful playback. Both
codec lifecycle suites pass. Formatting/SPDX, whitespace, Chinese incremental build
and final S31 checks pass. No device was flashed; physical fault recovery still needs
device fault injection to establish its hardware behavior.

## Review fix 05: retain incomplete application teardown

Application shutdown now shares one media cleanup path across startup failure,
control-worker exit and repeated stop calls. Physical/USB workers and RTC must all
stop before their PCM buffers or the application AOSL reference can be released.
Uninitialized physical media is skipped. STOPPED is published only after the HTTP
queue, lifecycle storage and media teardown have completed. Retained KV storage is
also reclaimed when lifecycle initialization failed before an HTTP join completed.

The public stop fault tests pass 16 control scenarios, including persistent physical,
USB, RTC, HTTP/control-join failures and successful later reclamation. UI/caption/face
regressions pass under UBSan. Formatting/SPDX and whitespace checks pass; the Chinese
incremental build and final S31 checks pass in build/korvo-s31. No device was flashed;
these fault results use mocked worker/vendor boundaries.

## Review fix 06: preserve languages across scenes

Scene selection now changes only the translation mode and audio source. It no longer
overwrites the accepted language pair with Chinese/English when crossing codec/USB
scenes. The existing UI flow selects Japanese/Korean and checks preserved labels and
commands through meeting, media, face-to-face and meeting again. Defaults remain at
initial configuration, and language changes still wait for acknowledged readback.

The Chinese/English UI suites pass on both screen layouts, along with font checks,
formatting/SPDX and whitespace checks. Chinese incremental firmware build and final
S31 checks pass in build/korvo-s31. No device was flashed.

## Review fix 07: reject stale physical key gestures

Physical AI/PLAY gestures capture their action and SDK attachment epoch on press.
Release dispatch validates that original gesture instead of selecting a new action
from the release-time state. A gesture invalidated by a session-state transition or
detach cannot become a start for a later session. Long-press provisioning remains
owned by the board.

Queued key events also carry the existing runtime and conversation generations.
The conversation generation now spans an entire start attempt, so a valid STOP
during STARTING still applies after RTC joins. Queue admission shares a short fence
with attempt cancellation reset; stale STOP callbacks cannot mute a later session.

Both-board input tests pass 31 scenarios, including the original ACTIVE-to-READY
failure reproduced before the fix, stale held gestures, detach/reattach and long
provisioning. UBSan control tests pass 20 UI scenarios plus the existing lifecycle,
caption and route suites. One case injects STOP after the RTC-start cancellation
precheck to verify that a starting attempt can still be stopped. Formatting/SPDX,
whitespace, Chinese incremental build and final S31 checks pass. No device was flashed;
physical key timing remains a separate hardware check.

## Review fix 08: unique composite USB identity

The USB serial string now comes from the factory MAC rather than the shared board
model string. Product and audio/console interface names are preserved. Identity-read
failure aborts descriptor configuration instead of exposing a shared fallback serial.

The pinned-header descriptor test passes under UBSan with two synthetic factory
MACs, stable repeat configuration, unchanged names, read errors and invalid identities.
Formatting/SPDX, whitespace, Chinese incremental firmware build and final S31 checks
pass in build/korvo-s31. No device was flashed; multi-device macOS enumeration is
still a hardware validation item.

## Review fix 09: register only initialized USB audio

Board capability registration now checks successful USB audio/core initialization.
Failed descriptor, UAC or controller startup does not expose meeting audio to the UI.
An unavailable CDC VFS console does not suppress a working UAC function. Cable and
stream-open state remain runtime readiness checks, separate from this capability.

Nine composite initialization fault cases pass under UBSan, and both-board input
and registration tests pass 39 scenarios. CI runs the new initialization suite.
The 61 builder/delivery tests pass with Python 3.11; formatting/SPDX, whitespace,
Chinese incremental firmware build and final S31 checks pass. No device was flashed;
USB enumeration and reconnect behavior still need hardware testing.

## Review fix 10: bind delivery source to the build

An ordered prebuild step captures the Git revision, dirty state, project build-input
digest and effective configuration hash. It generates a retained digest in the
application only when the snapshot changes. After application and flash segments
complete, a source recheck binds the snapshot to ELF/BIN/configuration/segment hashes.
The delivery checker consumes this frozen record and validates the embedded digest;
it never replaces provenance with the packaging checkout's HEAD. Missing or stale
records require a rebuild. Credentials/configuration contents are not recorded, and
the source digest explicitly covers project inputs rather than external toolchains.

The real Chinese build and merged-image packaging pass. A no-op build preserves
ELF, BIN, generated identity and provenance content and timestamps. Host tests cover
packaging A after checkout B, failed B preserving A, ordinary source edits without
reconfigure, dirty inputs, mid-build changes and artifact/embedded-digest mismatches.
All 74 builder/delivery tests pass with Python 3.11; the 22 delivery tests also pass
with IDF Python 3.10. Embedded Wi-Fi HTML and hidden-index modification regressions
pass. The final Chinese rebuild and S31 checks pass. No device was flashed.

## Review fix 11: allocation-free PSRAM task reclamation

The S31 AOSL adapter prepares a static internal queue, TCB and aligned 2 KiB reaper
stack before admitting a PSRAM worker. Normal return and explicit self-exit enqueue
the task handle and park; the permanent reaper uses IDF's external WithCaps deletion
path, which waits until the task is off every core before freeing it. Queue pressure
blocks without discarding ownership or allocating a temporary cleanup task. Reaper
startup failure rejects worker creation. Original AOSL inputs remain unchanged, and
the reaper depends only on FreeRTOS so it can safely outlive an AOSL reference cycle.

UBSan tests compile the unchanged HAL through the production wrapper and cover
startup rollback, overlapping creators, matching allocation/deletion, explicit exit,
and 24 worker exits with allocation denied and 16-slot queue backpressure. All pass;
formatting/SPDX and whitespace checks pass. Chinese firmware and final S31 checks
pass, and ELF inspection places every reaper resource in internal DRAM. No device
was flashed; host mocks do not establish real SMP shutdown timing or stack headroom.

## Review fix 12: explicit USB bus-power budget

Mosaico FS/HS descriptors now request 500 mA after configuration and the native PHY
configuration explicitly declares bus-powered operation. Pinned-header descriptor
tests verify bmAttributes=0x80 and bMaxPower=250 at both speeds; initialization tests
verify the explicit self_powered=false override. These tests pass under UBSan, with
formatting/SPDX and whitespace checks. This is a declared budget, not a measured
load or a hardware current limit; charging, inrush, preconfiguration/suspend and
full-volume load remain unmeasured. Chinese build and final S31 checks pass. No
device was flashed.
