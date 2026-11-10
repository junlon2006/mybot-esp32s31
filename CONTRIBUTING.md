# Contributing

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md)

The only supported target is ESP-Mosaico CoreBoard V1.2 with ESP-IDF v6.1 preview
and 60 ms RTSA audio frames. Keep changes focused and preserve unrelated work.

## Boundaries

Application startup and MyBot lifecycle belong in `main/`; board hardware, drivers and services belong in `components/mybot_platform/`. Use only the MyBot public headers under `components/mybot_stack/mybot_sdk/mybot/include/mybot/`.

The MyBot SDK is a project-maintained V2VT media/face-to-face/meeting fork. Preserve its 16 kHz mono S16 platform contract and 60 ms cadence. Local AEC is the Mosaico default; optional Cloud AEC retains its interleaved MIC/playback-reference transport and must not run together with Local AEC. Capture DSP resets complete synchronously on the capture owner before PCM fences are acknowledged. Keep AOSL, esp_codec_dev, LVGL, esp_lvgl_port and the pinned RTSA header/library unchanged, except for AOSL's component CMake wrapper and `s31_port/` adapter. The `mybot_sdk`, `aosl` and `agora_rtc` runtime components live directly under `components/mybot_stack/`. The legacy-named AOSL `esp32-s3` HAL is portable code and remains part of the immutable source input.

Never commit credentials, device identifiers, Wi-Fi passwords, original RTSA archives, generated configurations or firmware. Add SPDX headers to maintained C/C++ files and format changed files with `.clang-format` using exactly clang-format 18.1.8, matching CI. Install it with `python -m pip install clang-format==18.1.8` and confirm `clang-format --version`; distro packages named `clang-format-18` can contain a different patch release. Use the same version for generated LVGL sources. Update user documentation, `CHANGELOG.md`, source provenance and license notices when applicable.

## Validation

Install ffmpeg and ESP-IDF v6.1. The pinned RTSA payload is bundled. Build Chinese firmware directly with `idf.py`, always using `build/mosaico-v1.2` and `build/mosaico-v1.2/sdkconfig` locally:

```sh
. /path/to/esp-idf/export.sh
test "$(idf.py --preview --version)" = "ESP-IDF v6.1"
idf.py --preview -B build/mosaico-v1.2 \
  -DMYBOT_MOSAICO_UAC_TEST=OFF \
  -DSDKCONFIG=build/mosaico-v1.2/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;ci/ptime60.defaults" build size
python3 scripts/check_s31_build.py --build-dir build/mosaico-v1.2 --language zh-CN
git diff --check
```

CI retains isolated job directories and covers English using `ci/en-us.defaults`; the fixed local directory does not change CI's matrix. The [workflow](.github/workflows/ci.yml) defines the source checks, host tests and firmware variants. Run affected host tests under `tests/`; lifecycle or callback changes require fault-injection coverage for failures, timeout, repeated start/stop and resource ownership. The [release guide](docs/RELEASING.md) covers merging and delivery checks in the same local build directory.

Record source checks, host tests, target builds and physical-board results separately in [s31-validation.md](docs/s31-validation.md). Successful compilation does not validate touch interaction, acoustic quality or long-term stability. The S31 target requires `--preview`. Video stays disabled.

Pull requests should state the concrete problem, resulting behavior, validation and remaining hardware checks. Keep all dependency licenses and exact input revisions reviewable.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/), with a subject of at most 72 characters:

```text
<type>[optional scope][!]: <subject>
```

Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`. Separate the body from the subject with a blank line. Install the repository-local message validator with `./scripts/setup-githooks.sh` when desired.

Contributions use the repository license unless their files explicitly carry other applicable terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the retained third-party inputs.
