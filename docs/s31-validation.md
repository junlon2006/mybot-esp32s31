# S31 validation record

The latest local checks are under [Face-to-face translation](#2026-11-04-face-to-face-translation). Earlier sections describe the revision tested at that time. Use the manifests and checksums beside delivered images for their current binary identity.

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
