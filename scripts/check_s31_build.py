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
import struct
import sys

from capture_build_source import verify as verify_build_provenance

SUPPORTED_BOARD = "esp-mosaico-v1.2"
BOARD_CODEC = "CONFIG_CODEC_ES8311_SUPPORT"
FORBIDDEN_CODEC_CONFIGS = (
    "CONFIG_CODEC_ES7210_SUPPORT",
    "CONFIG_CODEC_ES7243_SUPPORT",
    "CONFIG_CODEC_ES7243E_SUPPORT",
    "CONFIG_CODEC_ES8156_SUPPORT",
    "CONFIG_CODEC_AW88298_SUPPORT",
    "CONFIG_CODEC_ES8389_SUPPORT",
    "CONFIG_CODEC_ES8374_SUPPORT",
    "CONFIG_CODEC_ES8388_SUPPORT",
    "CONFIG_CODEC_TAS5805M_SUPPORT",
    "CONFIG_CODEC_ZL38063_SUPPORT",
    "CONFIG_CODEC_CJC8910_SUPPORT",
    "CONFIG_CODEC_DUMMY_SUPPORT",
    "CONFIG_CODEC_DATA_ADC_SUPPORT",
)
RTSA_SHA256 = "7c6a81ab91f3b44c0cd581f84f779d7a022dac159020c19bdddd07c95da31dea"
RTSA_FILES = {
    "lib/riscv32/libagora-rtc-sdk.a": "573a00d58ad19cc0b66fc1e41f17e23ada34b77fe5c7a8c3e8fa8729f73fb6c5",
    "include/agora_rtc_api.h": "29b1c21cbe416ee703eaa197ce623469619b1ed68cd3e9c66cb3abe2fafec7db",
    "include/global_config.cmake": "acf697baa23c6b11143bf35b06564c0b924ef78e9d803f91d664e7673e014c21",
}
AOSL_SYMBOLS = ("aosl_malloc_impl", "aosl_mpq_create", "aosl_hal_thread_create", "aosl_hal_os_version")
USB_WRAPPED_SYMBOLS = ("dcd_edpt_xfer", "dcd_event_handler", "usbd_edpt_xfer_fifo")
FLASH_SIZE_BYTES = 16 * 1024 * 1024
MIN_OTA_FREE_BYTES = 300 * 1024


def parse_partition_table(data: bytes, table_offset: int) -> list[dict]:
    """Read IDF's 32-byte entries from the built table, including its MD5 record."""
    if not data or len(data) % 32 or len(data) > 0xC00:
        raise RuntimeError("invalid partition table length")
    if table_offset % 0x1000 or table_offset < 0 or table_offset + 0x1000 > FLASH_SIZE_BYTES:
        raise RuntimeError("invalid partition table flash offset")
    entries = []
    checksum_seen = False
    terminated = False
    for position in range(0, len(data), 32):
        record = data[position:position + 32]
        if record == b"\xff" * 32:
            if data[position:] != b"\xff" * (len(data) - position):
                raise RuntimeError("non-padding data after partition table terminator")
            terminated = True
            break
        if record[:2] == b"\xeb\xeb":
            if checksum_seen or record[:16] != b"\xeb\xeb" + b"\xff" * 14:
                raise RuntimeError("invalid partition table MD5 record")
            if record[16:] != hashlib.md5(data[:position]).digest():
                raise RuntimeError("partition table MD5 checksum mismatch")
            checksum_seen = True
            continue
        if checksum_seen:
            raise RuntimeError("partition entry after MD5 record")
        magic, kind, subtype, offset, size, raw_name, flags = struct.unpack("<2sBBII16sI", record)
        if magic != b"\xaa\x50":
            raise RuntimeError("invalid partition entry magic")
        try:
            name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        except UnicodeDecodeError as error:
            raise RuntimeError("invalid partition label") from error
        if not name or not name.isprintable():
            raise RuntimeError("invalid partition label")
        alignment = 0x10000 if kind == 0 else 0x1000
        if (not size or offset % alignment or (kind == 0 and size % 0x1000) or
                offset + size > FLASH_SIZE_BYTES):
            raise RuntimeError(f"invalid partition bounds or alignment: {name}")
        if offset < table_offset + 0x1000 and not (kind in (2, 3) and subtype == 0):
            raise RuntimeError(f"partition overlaps reserved boot/table region: {name}")
        if flags & ~3:
            raise RuntimeError(f"unsupported partition flags: {name}")
        entries.append({"name": name, "type": kind, "subtype": subtype,
                        "offset": offset, "size": size, "flags": flags})
    if not terminated or not entries:
        raise RuntimeError("partition table is empty or missing its terminator")
    names = [entry["name"] for entry in entries]
    if len(set(names)) != len(names):
        raise RuntimeError("duplicate partition labels")
    previous_end = 0
    ota_subtypes = set()
    for entry in sorted(entries, key=lambda item: item["offset"]):
        if entry["offset"] < previous_end:
            raise RuntimeError(f"overlapping partitions: {entry['name']}")
        previous_end = entry["offset"] + entry["size"]
        if entry["type"] == 0 and 0x10 <= entry["subtype"] <= 0x1F:
            if entry["subtype"] in ota_subtypes:
                raise RuntimeError("duplicate OTA partition subtype")
            ota_subtypes.add(entry["subtype"])
            if entry["flags"] & 2:
                raise RuntimeError(f"OTA partition must be writable: {entry['name']}")
    return entries


def verify_ota_capacity(build_dir: Path, description: dict, flash: dict, segments: list[dict]) -> dict:
    records = {}
    for role in ("partition-table", "app"):
        record = flash.get(role)
        if not isinstance(record, dict):
            raise RuntimeError(f"missing {role} flash record")
        try:
            offset, name = int(record["offset"], 0), record["file"]
        except (KeyError, TypeError, ValueError) as error:
            raise RuntimeError(f"invalid {role} flash record") from error
        if not any(int(segment["offset"], 0) == offset and segment["file"] == name for segment in segments):
            raise RuntimeError(f"{role} flash record does not match flash_files")
        records[role] = (offset, name)
    table_offset, table_name = records["partition-table"]
    app_offset, app_name = records["app"]
    if app_name != description["app_bin"]:
        raise RuntimeError("app flash record does not name the built application")
    entries = parse_partition_table(build_file(build_dir, table_name).read_bytes(), table_offset)
    flashed = next((entry for entry in entries if entry["offset"] == app_offset), None)
    if not flashed or flashed["type"] != 0:
        raise RuntimeError("flashed application offset must match an application partition start")
    app_size = build_file(build_dir, app_name).stat().st_size
    if app_size > flashed["size"]:
        raise RuntimeError("application exceeds its flashed partition")
    slots = [{"name": entry["name"], "offset": hex(entry["offset"]), "size": entry["size"],
              "free_bytes": entry["size"] - app_size} for entry in entries
             if entry["type"] == 0 and 0x10 <= entry["subtype"] <= 0x1F]
    if not slots:
        raise RuntimeError("partition table must contain writable OTA application slots")
    for slot in slots:
        if slot["free_bytes"] < MIN_OTA_FREE_BYTES:
            raise RuntimeError(f"OTA partition {slot['name']} has {slot['free_bytes']} free bytes; "
                               f"requires at least {MIN_OTA_FREE_BYTES}")
    return {"minimum_required_free_bytes": MIN_OTA_FREE_BYTES, "image_size_bytes": app_size,
            "flashed_partition": flashed["name"], "minimum_free_bytes": min(slot["free_bytes"] for slot in slots),
            "slots": slots}


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
    component = project_root / "components/mybot_stack/agora_rtc"
    for name, expected in RTSA_FILES.items():
        path = component / name
        if not path.is_file() or sha256(path) != expected:
            raise RuntimeError(f"bundled S31 RTSA fingerprint mismatch: {name}")


def live_memory_map(map_text: str) -> str:
    marker = re.search(r"^Linker script and memory map\s*$", map_text, re.MULTILINE)
    live_map = map_text[marker.end():] if marker else ""
    return re.split(r"^Cross Reference Table\s*$", live_map, maxsplit=1, flags=re.MULTILINE)[0]


def verify_project_uac_runtime(build_dir: Path, map_text: str) -> None:
    commands = json.loads(build_file(build_dir, "compile_commands.json").read_text())
    if not isinstance(commands, list):
        raise RuntimeError("invalid Mosaico UAC compile commands")
    for command in commands:
        if not isinstance(command, dict) or not isinstance(command.get("file"), str):
            raise RuntimeError("invalid Mosaico UAC compile command")
        if Path(command["file"]).name == "usb_device_uac.c":
            raise RuntimeError("Mosaico must not compile the vendor usb_device_uac.c runtime")
    live_map = live_memory_map(map_text)
    for symbol in ("uac_device_init", "usb_mic_task", "usb_spk_task"):
        if re.search(rf"\b{symbol}\b", live_map):
            raise RuntimeError(f"Mosaico must not link vendor UAC runtime symbol {symbol}")
    if re.search(r"\busb_device_uac(?:\.c)?\.(?:obj|o)\b", live_map):
        raise RuntimeError("Mosaico must not link the vendor usb_device_uac object")


def verify_usb_wrappers(build_dir: Path, app_elf: str, map_text: str) -> None:
    # IDF copies LINK_OPTIONS to the ELF during project(). A later build-property
    # append can leave valid-looking wrapper source completely unreferenced.
    ninja = build_file(build_dir, "build.ninja").read_text()
    ninja = re.sub(r"\$\r?\n[ \t]*", "", ninja)
    target = app_elf.replace("$", "$$").replace(" ", "$ ").replace(":", "$:")
    link = re.search(
        rf"^build {re.escape(target)}: \S*EXECUTABLE_LINKER\S*[^\n]*\n"
        r"((?:[ \t]+[^\n]*(?:\n|$))*)", ninja, re.MULTILINE,
    )
    flags = re.search(r"^[ \t]+LINK_FLAGS = (.*)$", link.group(1), re.MULTILINE) if link else None
    link_flags = flags.group(1) if flags else ""
    for symbol in USB_WRAPPED_SYMBOLS:
        if not re.search(rf"(?:^|[,\s])--wrap={symbol}(?=$|[,\s])", link_flags):
            raise RuntimeError(f"Mosaico UAC link must include --wrap={symbol} on {app_elf}")

    # Both the discarded-input section and the cross-reference table contain
    # symbol names even when --gc-sections removed the wrappers from the ELF.
    live_map = live_memory_map(map_text)
    for symbol in USB_WRAPPED_SYMBOLS:
        addresses = re.findall(
            rf"^[ \t]+0x([0-9a-fA-F]+)[ \t]+__wrap_{symbol}[ \t]*$",
            live_map, re.MULTILINE,
        )
        if not any(int(address, 16) for address in addresses):
            raise RuntimeError(f"Mosaico UAC wrapper __wrap_{symbol} must have a nonzero live map symbol")


def verify_build(build_dir: Path, language: str) -> dict:
    build_dir = build_dir.resolve()
    description = json.loads(build_file(build_dir, "project_description.json").read_text())
    if description.get("target") != "esp32s31" or description.get("git_revision") != "v6.1":
        raise RuntimeError("firmware must target esp32s31 with ESP-IDF v6.1")
    board_match = re.search(
        r"^MYBOT_BOARD:[^=]+=(.+)$",
        build_file(build_dir, "CMakeCache.txt").read_text(), re.MULTILINE,
    )
    board = board_match.group(1).strip() if board_match else None
    if board != SUPPORTED_BOARD:
        raise RuntimeError(f"unsupported or missing MYBOT_BOARD: expected {SUPPORTED_BOARD}, got {board}")
    config = dict(re.findall(
        r"^(CONFIG_\w+)=(.*)$", build_file(build_dir, "sdkconfig").read_text(), re.MULTILINE
    ))
    for symbol in FORBIDDEN_CODEC_CONFIGS:
        if config.get(symbol) == "y":
            raise RuntimeError(f"Mosaico V1.2 must not enable {symbol}")
    required = {
        "CONFIG_IDF_TARGET": '"esp32s31"',
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
        "CONFIG_SPIRAM": "y",
        "CONFIG_SPIRAM_MODE_OCT": "y",
        BOARD_CODEC: "y",
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
    if config.get("CONFIG_USB_DEVICE_UAC_AS_PART") == "y":
        verify_project_uac_runtime(build_dir, map_text)
        verify_usb_wrappers(build_dir, description["app_elf"], map_text)
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
        if start < previous_end or size == 0 or start + size > FLASH_SIZE_BYTES:
            raise RuntimeError("invalid, overlapping or oversized flash segment")
        previous_end = start + size
        segments.append({"offset": hex(start), "file": name, "size": size, "sha256": sha256(source)})
    if not segments or description["app_bin"] not in {segment["file"] for segment in segments}:
        raise RuntimeError("flash files must include the application")
    ota_capacity = verify_ota_capacity(build_dir, description, flash, segments)
    provenance = verify_build_provenance(build_dir)
    return {
        "schema_version": 1,
        "board": board,
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
        "source": provenance["snapshot"]["source"],
        "build_provenance": provenance,
        "ota_capacity": ota_capacity,
    }


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
        f'Board: {metadata["board"]}\nLanguage: {language}\nESP-IDF: v6.1\n'
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
    print(f'Validated {metadata["board"]} {args.language}: RISC-V, local AOSL, video off, '
          f'{len(metadata["segments"])} flash segments; '
          f'OTA free min={metadata["ota_capacity"]["minimum_free_bytes"]} bytes '
          f'(required={MIN_OTA_FREE_BYTES})')
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
