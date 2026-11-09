# Build validation and delivery

[English](RELEASING.md) · [简体中文](RELEASING.zh-CN.md)

The only delivery board is `esp-mosaico-v1.2`. RTSA v1.10.1 build 1366494 is bundled under `components/mybot_stack/agora_rtc`; direct builds need ESP-IDF v6.1 and ffmpeg, with no download/import step. RTSA input checks verify the pinned payload hashes. Audio remains 60 ms and video disabled.

## Local build

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
python3 scripts/build_all.py --language both \
  --build-root build/delivery --output-root releases/delivery
```

This is the same entrypoint used by CI. It creates isolated language build/sdkconfig
directories, then performs configure, build, size, merge and delivery validation.
CI supplies language-specific temporary build/output roots; local delivery uses the
dedicated roots above. For daily incremental development, continue using
`idf.py --preview -B build/korvo-s31`.

The checker validates target/configuration, pinned RTSA inputs, final links and exact
flash segments. It reads the built partition table and requires at least **300 KiB** free
in every OTA application slot. Generated roots are cleaned only after an ownership
marker check, so an unowned nonempty directory is rejected.

The build captures its Git revision, dirty state and project-input/configuration digest before
compilation, embeds that identity, then binds it to the completed ELF/BIN and flash segments.
Packaging uses this frozen record even after a checkout change. A missing or mismatched record
requires rebuilding; reconfiguring or editing delivery metadata cannot establish provenance.

## Delivery files

Each language produces four files under the selected output root, for example:

```text
esp-mosaico-v1.2-zh-CN.bin
esp-mosaico-v1.2-zh-CN.manifest.json
esp-mosaico-v1.2-zh-CN.sha256
esp-mosaico-v1.2-zh-CN.flash.txt
```

The manifest records project/IDF versions, build-bound source revision/dirty state, language, board/target, RTSA package/hash, artifact and merged-image hashes, segment offsets and OTA free space. It records build provenance, not hardware acceptance. Keep all four files together.

Merged images are flashed at **0x0**. They include padding over NVS and OTA state; provisioning is required afterwards. For daily development use `idf.py --preview -B build/korvo-s31 -p PORT flash` to preserve NVS. The runtime CDC port differs from its ROM download port; select the current port before running `idf.py --preview -B build/korvo-s31 -p RUNTIME_PORT monitor`.

## CI

Pushes and pull requests run source checks/host tests and build the Chinese/English
matrix through `scripts/build_all.py` using the bundled pinned RTSA input. Each job has
isolated temporary build/configuration paths and uploads the four delivery files. The
workflow uploads no original tgz, sdkconfig, link map or NVS data and creates no GitHub
Release automatically.

Local verification and remote workflow results are separate. Remote CI execution is pending until an actual run is recorded in [validation](s31-validation.md).

Before a release, update project version/changelog, confirm the active dependency notices and compare fresh checksums. Record test commands and results, distinguish host/build checks from physical-board results, and include current limitations. Publishing, tagging and committing require the user's explicit instruction; preparing local deliverables does not publish them.
