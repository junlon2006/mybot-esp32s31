#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Back up the overwritten NOR window, then flash the Mosaico V1.2 UAC test."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="native ROM /dev/cu.usbmodem... port after BOOT + power-on")
    parser.add_argument("--check-only", action="store_true", help="check images without opening USB")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = root / "build/mosaico-uac"
    description = json.loads((build / "project_description.json").read_text())
    if description.get("project_name") != "mosaico_uac_test":
        raise RuntimeError("build/mosaico-uac is not the Mosaico UAC test build")
    flash = json.loads((build / "flasher_args.json").read_text())
    if flash.get("extra_esptool_args", {}).get("chip") != "esp32s31":
        raise RuntimeError("unexpected flash target")
    segments = []
    for offset, name in sorted(flash["flash_files"].items(), key=lambda x: int(x[0], 0)):
        path = (build / name).resolve()
        if build.resolve() not in path.parents or not path.is_file():
            raise RuntimeError("invalid flash segment path")
        if path.stat().st_size == 0 or int(offset, 0) + path.stat().st_size > 0x100000:
            raise RuntimeError("test writes exceed the 1 MiB backup window")
        segments.extend([offset, str(path)])
    if len(segments) != 6:
        raise RuntimeError("expected exactly bootloader, partition table and application")
    print("Mosaico V1.2 UAC test: three segments fit inside the first 1 MiB", flush=True)
    if args.check_only:
        return
    if not args.port or "usbmodem" not in args.port:
        parser.error("enter ROM mode and pass its /dev/cu.usbmodem... port")
    backup_dir = Path(tempfile.mkdtemp(prefix="mosaico-original-", dir=build))
    backup = backup_dir / "nor-first-1MiB.bin"
    tool = [sys.executable, "-m", "esptool", "--chip", "esp32s31", "-p", args.port,
            "--before", "no-reset", "--no-stub"]
    print("Saving original firmware window to " + str(backup), flush=True)
    subprocess.run(tool + ["--after", "no-reset", "read-flash", "0", "0x100000", str(backup)],
                   check=True)
    if backup.stat().st_size != 0x100000:
        raise RuntimeError("incomplete backup; refusing to flash")
    digest = hashlib.sha256(backup.read_bytes()).hexdigest()
    backup.with_suffix(".sha256").write_text(digest + "  " + backup.name + "\n")
    restore = tool + ["--after", "hard-reset", "write-flash", "0", str(backup)]
    (backup_dir / "restore.txt").write_text(
        "Enter ROM mode again; update the port if it changed.\n" + shlex.join(restore) + "\n")
    settings = flash["flash_settings"]
    write = tool + ["--after", "hard-reset", "write-flash", "--flash-mode", settings["flash_mode"],
                    "--flash-freq", settings["flash_freq"], "--flash-size", settings["flash_size"]]
    subprocess.run(write + segments, check=True)
    print("Flashed. Original firmware restore command: " + str(backup_dir / "restore.txt"))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
