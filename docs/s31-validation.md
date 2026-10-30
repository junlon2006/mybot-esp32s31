# S31 first-version validation — 2026-10-30

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
- The current update adds press/release conversation start/stop through the public SDK key callback, retains physical PLAY/SET/VOL, and removes unused S3 project material while preserving dependency snapshots. It includes bundled pinned RTSA payloads, push/PR language builds and per-language checked delivery metadata.
- New touch real-device acceptance and remote CI execution are pending. Current local source/test/build results must be recorded after verification; the initial-build results and SHA-256 table above must not be attributed to this revision.
- Other audio tuning, input/network regression, endurance/resource measurement and extension work are deferred at the user's request. Video remains disabled.

## Touch control, CI delivery and S31-only baseline — 2026-10-30

- The user has confirmed normal screen display as well as the previously recorded bidirectional audio test. New screen-touch start/stop behavior has not yet been hardware-accepted.
- Touch input now submits one state-specific command on release through a nonblocking mailbox to the existing key worker. Waiting/long press does not repeat; read errors cancel a gesture; unready and detached SDK generations cannot replay stale touches. Physical keys remain available.
- After removing unrelated S3 components/profiles/tests, local input/board (17 cases), display (23 cases), shared PCM buffer, audio lifecycle and HTTPS trace (20 synthetic scenarios) passed. Input/display also passed UBSan. Audio source and the MyBot/AOSL/codec/LVGL/port snapshots remained byte-identical.
- Builder and delivery tests: 52 cases under local Python 3.10, 47 passed and 5 Python 3.11+ filesystem-cleanup cases skipped. The CI quality job selects Python 3.12 to exercise those cases. Project formatting, SPDX, whitespace and workflow YAML/Bash checks passed.
- Direct IDF v6.1 preview builds and size checks passed for both languages in the cleaned target project. The actual absolute-path merge-bin and shared checker flow passed for both; each of four flash segments, target/config, bundled RTSA fingerprint, RISC-V ELF and local AOSL map ownership was verified.
- The approved original RTSA library/header/config/package identity are now versioned inputs. No CI URL, secret or import is required. Push/PR/manual workflows automatically build both languages and upload the four delivery files; remote Actions execution has not been performed locally.
- Delivery files are `.bin`, `.manifest.json`, `.sha256` and `.flash.txt` per language. Flash instructions derive reset, no-stub and flash settings from that build. The merged image starts at 0x0.

The following hashes describe the local verification images. Final delivery checksums are in each release `.sha256` / `.manifest.json`.

| Language | Merged bytes | SHA-256 |
| --- | --- | --- |
| zh-CN | 2699216 | `6bdbbb77f662f4d8bf190b27d46c875f0e2742dfaf97f016c47a89c6b513756e` |
| en-US | 2779872 | `cf15cadf9b172862b2ecd5280d76b9b94a28c80768f73dea90bb6c7998a18fbf` |

Other requested-deferred features, audio tuning, endurance testing and video were not added in this update.

### Clean repository-input verification

The exact staged Git blobs were exported to a new source directory with no local build outputs, managed-component directory or ignored SDK archive. All tracked MyBot/AOSL/codec/LVGL/port source bytes (including original line endings) matched the working inputs. The optional, unused Xtensa-only ZL38063 archive was absent. A fresh Chinese IDF v6.1 preview build, size report, absolute-path merge and shared delivery checker all passed from this export. The final target-project Chinese and English builds also passed after adding the S31 guard against that unsupported codec. No remote GitHub Actions run or new touch hardware test is claimed.

This source baseline is maintained on the local `dev` branch. Delivery manifests record its exact revision and dirty status. Remote GitHub Actions has not been triggered.
