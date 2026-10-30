#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate S31 link/configuration and describe an existing merged flash image.

Used by both local builds and CI. Only firmware and small delivery metadata are
written; the raw RTSA library, sdkconfig and build paths are not packaged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

BOARD = "esp32-s31-korvo-1"
RTSA_SHA256 = "7c6a81ab91f3b44c0cd581f84f779d7a022dac159020c19bdddd07c95da31dea"
RTSA_FILES = {
    "lib/riscv32/libagora-rtc-sdk.a": "573a00d58ad19cc0b66fc1e41f17e23ada34b77fe5c7a8c3e8fa8729f73fb6c5",
    "include/agora_rtc_api.h": "29b1c21cbe416ee703eaa197ce623469619b1ed68cd3e9c66cb3abe2fafec7db",
    "include/global_config.cmake": "acf697baa23c6b11143bf35b06564c0b924ef78e9d803f91d664e7673e014c21",
}
AOSL_SYMBOLS = ("aosl_malloc_impl", "aosl_mpq_create", "aosl_hal_thread_create", "aosl_hal_os_version")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_file(build_dir: Path, name: str) -> Path:
    if Path(name).is_absolute() or ".." in Path(name).parts:
        raise RuntimeError(f"invalid build file: {name}")
    path = (build_dir / name).resolve()
    if build_dir not in path.parents or not path.is_file():
        raise RuntimeError(f"invalid build file: {name}")
    return path


def verify_bundled_rtsa(project_root: Path) -> None:
    vendor = project_root / "components/mybot_platform/ports/agora_rtc/vendor"
    for name, expected in RTSA_FILES.items():
        path = vendor / name
        if not path.is_file() or sha256(path) != expected:
            raise RuntimeError(f"bundled S31 RTSA fingerprint mismatch: {name}")


def verify_build(build_dir: Path, language: str) -> dict:
    build_dir = build_dir.resolve()
    description = json.loads(build_file(build_dir, "project_description.json").read_text())
    if description.get("target") != "esp32s31" or description.get("git_revision") != "v6.1":
        raise RuntimeError("firmware must target esp32s31 with ESP-IDF v6.1")
    config = dict(re.findall(
        r"^(CONFIG_\w+)=(.*)$", build_file(build_dir, "sdkconfig").read_text(), re.MULTILINE
    ))
    required = {
        "CONFIG_IDF_TARGET": '"esp32s31"',
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
        "CONFIG_SPIRAM": "y",
        "CONFIG_SPIRAM_MODE_OCT": "y",
        "CONFIG_CODEC_ES8389_SUPPORT": "y",
        "CONFIG_MYBOT_LVGL_UI": "y",
        "CONFIG_MYBOT_AUDIO_PTIME_60": "y",
        "CONFIG_MYBOT_AUDIO_PTIME_MS": "60",
        "CONFIG_MYBOT_LANGUAGE_TAG": f'"{language}"',
    }
    for symbol, expected in required.items():
        if config.get(symbol) != expected:
            raise RuntimeError(f"{symbol} must equal {expected}")
    if config.get("CONFIG_MYBOT_ENABLE_VIDEO") == "y":
        raise RuntimeError("video must remain disabled for the S31 delivery")
    elf = build_file(build_dir, description["app_elf"])
    with elf.open("rb") as stream:
        header = stream.read(52)
    if len(header) < 52 or header[:6] != b"\x7fELF\x01\x01" or int.from_bytes(header[18:20], "little") != 243:
        raise RuntimeError("application must be ELF32 little-endian RISC-V")
    map_text = build_file(build_dir, f'{description["project_name"]}.map').read_text()
    members = set(re.findall(r"libagora-rtc-sdk\.a\(([^)]+)\)", map_text))
    if not members or any(not name.endswith(".obj") for name in members):
        raise RuntimeError("missing RTSA link or vendor host AOSL archive member selected")
    for symbol in AOSL_SYMBOLS:
        if not re.search(rf"^{symbol}\s+esp-idf/aosl/libaosl\.a\(", map_text, re.MULTILINE):
            raise RuntimeError(f"{symbol} must come from local S31 AOSL")
    flash = json.loads(build_file(build_dir, "flasher_args.json").read_text())
    if flash.get("extra_esptool_args", {}).get("chip") != "esp32s31":
        raise RuntimeError("flash configuration must target esp32s31")
    extra_args = flash["extra_esptool_args"]
    if type(extra_args.get("stub")) is not bool or any(
        not isinstance(extra_args.get(key), str) or not extra_args[key]
        for key in ("before", "after")
    ):
        raise RuntimeError("flash configuration must specify reset modes and stub selection")
    if flash.get("flash_settings", {}).get("flash_size") != "16MB":
        raise RuntimeError("flash configuration must use 16MB")
    segments = []
    previous_end = 0
    for offset, name in sorted(flash["flash_files"].items(), key=lambda item: int(item[0], 0)):
        start = int(offset, 0)
        source = build_file(build_dir, name)
        size = source.stat().st_size
        if start < previous_end or size == 0 or start + size > 16 * 1024 * 1024:
            raise RuntimeError("invalid, overlapping or oversized flash segment")
        previous_end = start + size
        segments.append({"offset": hex(start), "file": name, "size": size, "sha256": sha256(source)})
    if not segments or description["app_bin"] not in {segment["file"] for segment in segments}:
        raise RuntimeError("flash files must include the application")
    return {
        "schema_version": 1,
        "board": BOARD,
        "target": "esp32s31",
        "language": language,
        "project_version": description["project_version"],
        "idf_version": "v6.1",
        "audio_ptime_ms": 60,
        "video_enabled": False,
        "rtsa": {
            "version": "1.10.1", "build": "1366494", "archive_sha256": RTSA_SHA256,
            "library_sha256": RTSA_FILES["lib/riscv32/libagora-rtc-sdk.a"],
        },
        "link_checks": {"elf_machine": "RISC-V", "local_aosl": True, "rtsa_members": len(members)},
        "flash_settings": flash["flash_settings"],
        "extra_esptool_args": extra_args,
        "segments": segments,
    }


def source_revision(project_root: Path) -> dict:
    try:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=project_root, check=True, capture_output=True, text=True
        ).stdout.strip()
        dirty = bool(subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=normal"], cwd=project_root,
            check=True, capture_output=True, text=True,
        ).stdout.strip())
        return {"revision": revision, "dirty": dirty}
    except (OSError, subprocess.CalledProcessError):
        return {"revision": None, "dirty": None}


def package(build_dir: Path, merged: Path, language: str) -> dict:
    metadata = verify_build(build_dir, language)
    if merged.is_symlink() or not merged.is_file() or any(parent.is_symlink() for parent in merged.parents):
        raise RuntimeError("merged image must be a regular file without symlink ancestors")
    merged = merged.resolve()
    # merge-bin runs inside its build directory. Absolute output paths and byte
    # comparisons prevent a successful job from uploading an image from elsewhere.
    with merged.open("rb") as image:
        for segment in metadata["segments"]:
            image.seek(int(segment["offset"], 0))
            digest = hashlib.sha256(image.read(segment["size"])).hexdigest()
            if digest != segment["sha256"]:
                raise RuntimeError(f'merged image does not contain {segment["file"]} at {segment["offset"]}')
    if merged.stat().st_size != max(int(s["offset"], 0) + s["size"] for s in metadata["segments"]):
        raise RuntimeError("merged image must start at flash offset 0x0 without trailing data")
    metadata["image"] = {
        "file": merged.name, "flash_offset": "0x0",
        "size": merged.stat().st_size, "sha256": sha256(merged),
    }
    metadata["source"] = source_revision(Path(__file__).resolve().parent.parent)
    extra_args = metadata["extra_esptool_args"]
    flash_command = [
        "python", "-m", "esptool", "--chip", extra_args["chip"], "--port", "PORT",
        "--before", extra_args["before"], "--after", extra_args["after"],
    ]
    if not extra_args["stub"]:
        flash_command.append("--no-stub")
    flash_command.append("write-flash")
    for key in ("flash_mode", "flash_freq", "flash_size"):
        flash_command.extend(["--" + key.replace("_", "-"), metadata["flash_settings"][key]])
    flash_command.extend(["0x0", merged.name])
    metadata["flash_command"] = flash_command
    manifest = merged.with_suffix(".manifest.json")
    checksum = merged.with_suffix(".sha256")
    instructions = merged.with_suffix(".flash.txt")
    for destination in (manifest, checksum, instructions):
        if destination.is_symlink():
            raise RuntimeError(f"refusing symlink output: {destination}")
    manifest.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    checksum.write_text(f'{metadata["image"]["sha256"]}  {merged.name}\n', encoding="utf-8")
    instructions.write_text(
        f'Board: {BOARD}\nLanguage: {language}\nESP-IDF: v6.1\n'
        'Verify: ' + shlex.join(["shasum", "-a", "256", "-c", checksum.name]) + '\n'
        'Flash from this directory (replace PORT):\n'
        + shlex.join(flash_command) + '\n'
        'Merged image includes bootloader, partition table, OTA data and application.\n'
        'Full-image flashing may reset stored Wi-Fi, pairing and volume settings.\n'
        'Individual source offsets and hashes are recorded in the manifest.\n', encoding="utf-8",
    )
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--language", choices=("zh-CN", "en-US"))
    parser.add_argument(
        "--vendor-only", action="store_true", help="verify the bundled RTSA inputs without a build"
    )
    parser.add_argument("--merged", type=Path, help="existing merge-bin image to verify and describe")
    args = parser.parse_args()
    verify_bundled_rtsa(Path(__file__).resolve().parent.parent)
    if args.vendor_only:
        if args.build_dir or args.language or args.merged:
            parser.error("--vendor-only does not accept build or image arguments")
        print("Verified bundled ESP32-S31 RTSA v1.10.1 build 1366494 fingerprints")
        return 0
    if not args.build_dir or not args.language:
        parser.error("--build-dir and --language are required unless --vendor-only is used")
    metadata = (
        package(args.build_dir, args.merged, args.language) if args.merged
        else verify_build(args.build_dir, args.language)
    )
    print(f'Validated {BOARD} {args.language}: RISC-V, local AOSL, video off, '
          f'{len(metadata["segments"])} flash segments')
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
