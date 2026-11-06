# Build validation and delivery

[English](RELEASING.md) · [简体中文](RELEASING.zh-CN.md)

The default delivery board is `esp-mosaico-v1.2`; the earlier Korvo profile is retained. RTSA v1.10.1 build 1366494 is bundled under `components/mybot_stack/agora_rtc`; direct builds need ESP-IDF v6.1 and ffmpeg, with no download/import step. RTSA input checks verify the pinned payload hashes. Audio remains 60 ms and video disabled.

## Local build

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
idf.py --preview -B build/korvo-s31 \
  -DMYBOT_BOARD=esp-mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/korvo-s31/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build size
python3 scripts/check_s31_build.py --build-dir build/korvo-s31 --language zh-CN
```

All local firmware builds use `build/korvo-s31` and its `sdkconfig`, including incremental builds.
When changing board or switching from a diagnostic, save any required firmware first and follow
the [board guide](esp-mosaico-v1.2.zh-CN.md) for cleaning and regenerating configuration.
CI keeps separate directories for each board/language job.

To merge this build and write its delivery metadata:

```sh
mkdir -p releases
idf.py --preview -B build/korvo-s31 merge-bin \
  -o "$PWD/releases/esp-mosaico-v1.2-zh-CN.bin"
python3 scripts/check_s31_build.py --build-dir build/korvo-s31 --language zh-CN \
  --merged releases/esp-mosaico-v1.2-zh-CN.bin
```

`--merged` validates an existing merged image and writes companion metadata; it does not compile or merge firmware itself. The checker validates target/configuration, pinned RTSA inputs, final links and exact flash segments. It reads the built partition table and requires at least **300 KiB** free in every OTA application slot.

The build captures its Git revision, dirty state and project-input/configuration digest before
compilation, embeds that identity, then binds it to the completed ELF/BIN and flash segments.
Packaging uses this frozen record even after a checkout change. A missing or mismatched record
requires rebuilding; reconfiguring or editing delivery metadata cannot establish provenance.

## Delivery files

Each language produces four files, for example:

```text
esp-mosaico-v1.2-zh-CN.bin
esp-mosaico-v1.2-zh-CN.manifest.json
esp-mosaico-v1.2-zh-CN.sha256
esp-mosaico-v1.2-zh-CN.flash.txt
```

The manifest records project/IDF versions, build-bound source revision/dirty state, language, board/target, RTSA package/hash, artifact and merged-image hashes, segment offsets and OTA free space. It records build provenance, not hardware acceptance. Keep all four files together.

Merged images are flashed at **0x0**. They include padding over NVS and OTA state; provisioning is required afterwards. For daily development use `idf.py --preview -B build/korvo-s31 -p PORT flash` to preserve NVS. Mosaico's runtime CDC port differs from its ROM download port; select the current port before running `idf.py --preview -B build/korvo-s31 -p RUNTIME_PORT monitor`.

## CI

Pushes and pull requests run source checks/host tests and build the Mosaico/Korvo × Chinese/English matrix using the bundled pinned RTSA input. Each job has isolated build/configuration paths and uploads the four delivery files. The workflow uploads no original tgz, sdkconfig, link map or NVS data and creates no GitHub Release automatically.

Local verification and remote workflow results are separate. Remote CI execution is pending until an actual run is recorded in [validation](s31-validation.md).

Before a release, update project version/changelog, confirm the active dependency notices and compare fresh checksums. Record test commands and results, distinguish host/build checks from physical-board results, and include current limitations. Publishing, tagging and committing require the user's explicit instruction; preparing local deliverables does not publish them.
