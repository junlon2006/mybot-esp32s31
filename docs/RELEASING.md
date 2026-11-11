# Build validation and delivery

[English](RELEASING.md) · [简体中文](RELEASING.zh-CN.md)

The only delivery board is `esp-mosaico-v1.2`. RTSA v1.10.1 build 1366494 is bundled under `components/mybot_stack/agora_rtc`; direct builds need ESP-IDF v6.1 and ffmpeg, with no download/import step. RTSA input checks verify the pinned payload hashes. Audio remains 60 ms and video disabled.

## Local build

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
idf.py --preview -B build/mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/mosaico-v1.2/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" menuconfig
idf.py --preview -B build/mosaico-v1.2 build
idf.py --preview -B build/mosaico-v1.2 size
```

All local firmware builds use `build/mosaico-v1.2` and its `sdkconfig`. In the
`mybot` menu select the language and Echo cancellation mode. New configurations
default to Local AEC; existing configurations retain their choice, so new defaults
do not switch an older Cloud AEC configuration. Change the mode through menuconfig
before rebuilding. Local and Cloud AEC are alternatives; disabled AEC is diagnostic.

The local builder supports the activated IDF Python 3.10+ environment:

```sh
python3 scripts/build_all.py --dry-run
python3 scripts/build_all.py
```

By default it builds and packages Chinese then English Local AEC in the same
`build/mosaico-v1.2`, leaving the saved configuration in English. Select one
language with `--language zh-CN` or `--language en-US`, and AEC with `--aec-mode`.
IDF's configuration server switches language/AEC/60 ms choices while retaining
other settings. Local builds are incremental and preserve build siblings and
unrelated output files, updating the selected variants' five delivery files.
Python 3.11 and `--no-clean` are not required.

The checker validates target/configuration, the expected AEC mode and compiled flags,
pinned RTSA inputs, final links and exact flash segments. For Local AEC it also checks
live AEC symbols and their S31 ESP-SR archive paths in the map. It reads the built
partition table and requires at least **300 KiB** free in every OTA application slot.
These checks establish build consistency, not acoustic performance.

The build captures its Git revision, dirty state and project-input/configuration digest before
compilation, embeds that identity, then binds it to the completed ELF/BIN and flash segments.
Packaging uses this frozen record even after a checkout change. A missing or mismatched record
requires rebuilding; reconfiguring or editing delivery metadata cannot establish provenance.

For a Chinese Local AEC delivery from that build:

```sh
mkdir -p releases
idf.py --preview -B build/mosaico-v1.2 merge-bin \
  -o "$PWD/releases/esp-mosaico-v1.2-zh-CN.bin"
python3 scripts/check_s31_build.py --build-dir build/mosaico-v1.2 \
  --language zh-CN --aec-mode local --merged releases/esp-mosaico-v1.2-zh-CN.bin
```

Set `--language` and `--aec-mode` to match the built configuration. The checker and
CI builder accept `local`, `cloud` and `disabled`, defaulting to `local`; a mode mismatch
fails validation. Keep deliveries of different modes separate, since filenames retain
the board/language naming below.

## Delivery files

Each language produces five files under the selected output root, for example:

```text
esp-mosaico-v1.2-zh-CN.bin
esp-mosaico-v1.2-zh-CN.manifest.json
esp-mosaico-v1.2-zh-CN.sha256
esp-mosaico-v1.2-zh-CN.flash.txt
esp-mosaico-v1.2-zh-CN.licenses.txt
```

The manifest records project/IDF versions, build-bound source revision/dirty state, language, actual `aec_mode`, board/target, RTSA package/hash, artifact and merged-image hashes, segment offsets and OTA free space. It records build provenance, not hardware acceptance. Keep all five files together. The `.licenses.txt` companion contains the third-party notices, project Apache-2.0 text, retained ESP-SR/ESP-DL/esp_new_jpeg package license texts and the fonts' full SIL OFL 1.1; include it when distributing the image.

Merged images are flashed at **0x0**. They include padding over NVS and OTA state; provisioning is required afterwards. For daily development use `idf.py --preview -B build/mosaico-v1.2 -p PORT flash` to preserve NVS. The runtime CDC port differs from its ROM download port; select the current port before running `idf.py --preview -B build/mosaico-v1.2 -p RUNTIME_PORT monitor`.

## CI

Pushes and pull requests run source checks/host tests and three firmware jobs:
Chinese Local AEC, English Local AEC and Chinese Cloud AEC. Each calls
`scripts/build_all.py --language LANGUAGE --aec-mode MODE` using the bundled pinned
RTSA input, with isolated temporary build/configuration and output roots. Generated
roots are cleaned only after an ownership-marker check; an unowned nonempty directory
is rejected. Each job uploads the five delivery files in an artifact whose name includes
its AEC mode. The workflow uploads no original tgz, sdkconfig, link map or NVS data and creates no GitHub
Release automatically.

An explicit `--build-root` selects this isolated workflow. Cleanup uses directory
descriptors and no-follow operations on Python 3.10+; `--no-clean` preserves owned
roots. CI should use a separate new/empty output directory: nonempty `releases/`
from local packaging has no cleanup ownership marker and cannot be reused as an
isolated cleanup root. The quality job uses Python 3.10 to cover the same runtime.

Local verification and remote workflow results are separate. Remote CI execution is pending until an actual run is recorded in [validation](s31-validation.md).

Before a release, update project version/changelog, confirm the active dependency notices and compare fresh checksums. Record test commands and results, distinguish host/build checks from physical-board results, and include current limitations. Publishing, tagging and committing require the user's explicit instruction; preparing local deliverables does not publish them.
