# Contributing

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md)

This repository supports ESP32-S31-Korvo-1 V1.1 only, using ESP-IDF v6.1 and 60 ms RTSA audio frames. Keep changes focused and preserve unrelated work.

## Boundaries

Application startup and MyBot lifecycle belong in `main/`; board hardware, drivers and services belong in `components/mybot_platform/`. Use only the MyBot public headers under `components/mybot_stack/mybot_sdk/mybot/include/mybot/`.

Keep the MyBot, AOSL, esp_codec_dev, LVGL and esp_lvgl_port source snapshots unchanged. S31 AOSL and RTSA wrappers live under `components/mybot_platform/ports/`; local component CMake wrappers may register the required sources without patching the snapshots. The legacy-named AOSL `esp32-s3` HAL is portable code used by the current S31 wrapper and remains part of the immutable source input.

Never commit credentials, device identifiers, Wi-Fi passwords, original RTSA archives, generated configurations or firmware. Add SPDX headers to maintained C/C++ files and format changed files with `.clang-format`. Update user documentation, `CHANGELOG.md`, source provenance and license notices when applicable.

## Validation

Install ffmpeg and ESP-IDF v6.1. The pinned RTSA payload is bundled, so development builds use `idf.py` directly:

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --version)" = "ESP-IDF v6.1"
idf.py --preview -B build/contribution \
  -DMYBOT_BOARD=esp32-s31-korvo-1 \
  -DSDKCONFIG=build/contribution/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build size
python3 scripts/check_s31_build.py --build-dir build/contribution --language zh-CN
git diff --check
```

Build English in a separate directory with `ci/en-us.defaults`. The [workflow](.github/workflows/ci.yml) defines the source checks, host tests and firmware variants. Run affected host tests under `tests/`; lifecycle or callback changes require fault-injection coverage for failures, timeout, repeated start/stop and resource ownership. The [release guide](docs/RELEASING.md) covers both-language packaging.

Record source checks, host tests, target builds and physical-board results separately in [s31-validation.md](docs/s31-validation.md). Successful compilation does not validate touch interaction, acoustic quality or long-term stability. The S31 target requires `--preview`. Video stays disabled.

Pull requests should state the concrete problem, resulting behavior, validation and remaining hardware checks. Keep all dependency licenses and exact input revisions reviewable.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/), with a subject of at most 72 characters:

```text
<type>[optional scope][!]: <subject>
```

Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`. Separate the body from the subject with a blank line. Install the repository-local message validator with `./scripts/setup-githooks.sh` when desired.

Contributions use the repository license unless their files explicitly carry other applicable terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the retained third-party inputs.
