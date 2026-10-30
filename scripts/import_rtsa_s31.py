#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Import the user-supplied, pinned S31 RTSA package without running its tools."""
import argparse
import hashlib
from pathlib import Path
import tarfile

ARCHIVE_SHA256 = "7c6a81ab91f3b44c0cd581f84f779d7a022dac159020c19bdddd07c95da31dea"
FILES = {
    "agora_rtsa_sdk/agora_sdk/include/agora_rtc_api.h": "include/agora_rtc_api.h",
    "agora_rtsa_sdk/agora_sdk/include/global_config.cmake": "include/global_config.cmake",
    "agora_rtsa_sdk/agora_sdk/lib/riscv32/libagora-rtc-sdk.a": "lib/riscv32/libagora-rtc-sdk.a",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    digest_state = hashlib.sha256()
    with args.archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest_state.update(chunk)
    digest = digest_state.hexdigest()
    if digest != ARCHIVE_SHA256:
        parser.error(f"unexpected RTSA archive SHA-256: {digest}")
    root = Path(__file__).resolve().parent.parent
    vendor = root / "components/mybot_platform/ports/agora_rtc/vendor"
    payloads = {}
    with tarfile.open(args.archive, "r:gz") as archive:
        members = {}
        for member in archive.getmembers():
            # Read only the explicitly approved members; never extract archive paths.
            name = member.name[2:] if member.name.startswith("./") else member.name
            if name in FILES:
                if name in members or not member.isfile() or member.size > 32 * 1024 * 1024:
                    parser.error(f"invalid package member: {name}")
                members[name] = member
        for name, relative in FILES.items():
            if name not in members:
                parser.error(f"missing package member: {name}")
            with archive.extractfile(members[name]) as stream:
                payloads[relative] = stream.read()
    # Validate all destinations before creating any directory or writing a file.
    for relative in payloads:
        destination = vendor / relative
        if destination.is_symlink() or any(parent.is_symlink() for parent in destination.parents):
            parser.error(f"refusing symlink destination: {destination}")
    for relative, data in payloads.items():
        destination = vendor / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    print(f"Imported pinned ESP32-S31 RTSA v1.10.1 build 1366494 to {vendor}")


if __name__ == "__main__":
    main()
