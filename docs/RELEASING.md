# Build validation and delivery

[English](RELEASING.md) · [简体中文](RELEASING.zh-CN.md)

Only `esp32-s31-korvo-1` is supported. RTSA v1.10.1 build 1366494 is bundled under `components/mybot_platform/ports/agora_rtc/vendor`; direct builds need ESP-IDF v6.1 and ffmpeg, with no download/import step. RTSA input checks verify the pinned payload hashes. Audio remains 60 ms and video disabled.

## Local build

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --version)" = "ESP-IDF v6.1"
python3 scripts/build_all.py --board esp32-s31-korvo-1 --language both
```

A normal builder run cleans `build/` and `releases/`; use `--no-clean` for incremental work. Python 3.11+ is required for the safe cleanup path. Development can continue to use [direct idf.py commands](esp32-s31-korvo-1.en.md).

The builder runs reconfigure/build/size/merge-bin for each language and validates the resulting image and build metadata. To inspect an existing direct build or package its merged image:

```sh
python3 scripts/check_s31_build.py --build-dir build/korvo-s31 --language zh-CN
python3 scripts/check_s31_build.py --build-dir build/korvo-s31 --language zh-CN \
  --merged releases/esp32-s31-korvo-1-zh-CN.bin
```

`--merged` validates an existing merged image and writes companion metadata; it does not compile or merge firmware itself. The checker validates target/configuration, the bundled RTSA payload, final link provenance, partition/image bounds and exact embedded flash segments.

## Delivery files

Each language produces four files, for example:

```text
esp32-s31-korvo-1-zh-CN.bin
esp32-s31-korvo-1-zh-CN.manifest.json
esp32-s31-korvo-1-zh-CN.sha256
esp32-s31-korvo-1-zh-CN.flash.txt
```

The manifest records project/IDF versions, source revision and dirty state, language, board/target, RTSA package/hash, merged-image hash and actual segment offsets/hashes. It records build provenance, not hardware acceptance. Keep all four files together.

Merged images are flashed at **0x0**. They include padding over NVS and OTA state; provisioning is required afterwards. For daily development use `idf.py --preview -B <build-dir> -p PORT flash monitor` to preserve NVS.

## CI

Pushes and pull requests run source checks/host tests and automatically build the Chinese and English firmware using the bundled pinned RTSA input. Each language has its own artifact containing the four delivery files. The workflow uploads no original tgz, sdkconfig, link map or NVS data and creates no GitHub Release automatically.

Local verification and remote workflow results are separate. Remote CI execution is pending until an actual run is recorded in [validation](s31-validation.md).

Before a release, update project version/changelog, confirm the active dependency notices and compare fresh checksums. Record test commands and results, distinguish host/build checks from physical-board results, and include current limitations. Publishing, tagging and committing require the user's explicit instruction; preparing local deliverables does not publish them.
