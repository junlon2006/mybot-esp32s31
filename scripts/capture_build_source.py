#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bind project build inputs to the executable and completed flash artifacts."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

SNAPSHOT = "mybot_build_source.json"
GENERATED_C = "mybot_build_source.c"
STAMP = "mybot_build_provenance.json"
MARKER = "MYBOT_BUILD_SOURCE_SHA256="
SOURCE_ROOTS = ("components/", "main/", "diagnostics/mosaico_uac/")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".s", ".a", ".cmake", ".ld", ".lf", ".in", ".ogg", ".html"}
SOURCE_FILES = {"CMakeLists.txt", "sdkconfig.defaults", "dependencies.lock",
                "scripts/capture_build_source.py", "scripts/build_source.cmake"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def encoded(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def source_input(name: str) -> bool:
    path = Path(name)
    return (name in SOURCE_FILES or
            (name.startswith("ci/") and path.suffix == ".defaults") or
            (name.startswith("partitions/") and path.suffix == ".csv") or
            (name.startswith(SOURCE_ROOTS) and
             (path.suffix.lower() in SOURCE_SUFFIXES or path.name.startswith("Kconfig") or
              path.name in {"CMakeLists.txt", "sdkconfig.defaults", "dependencies.lock", "idf_component.yml"})))


def snapshot(project_root: Path, build_dir: Path) -> dict:
    def git(*args: str) -> bytes:
        return subprocess.run(["git", *args], cwd=project_root, check=True,
                              stdout=subprocess.PIPE).stdout

    revision = git("rev-parse", "HEAD").decode().strip()
    tracked = {name.decode("utf-8") for name in git("ls-files", "-z").split(b"\0") if name}
    untracked = {name.decode("utf-8") for name in git("ls-files", "--others", "--exclude-standard", "-z").split(b"\0") if name}
    names = sorted(name for name in tracked | untracked if source_input(name))
    inputs = []
    for name in names:
        path = project_root / name
        if path.is_symlink():
            raise RuntimeError(f"source input must not be a symlink: {name}")
        inputs.append([name, sha256(path) if path.is_file() else None])
    dirty = bool(git("status", "--porcelain", "--untracked-files=no").strip())
    # These index flags can hide working-tree edits from status. Do not claim
    # a clean build when Git was instructed to skip checking a tracked file.
    flagged = git("ls-files", "-v", "-z").split(b"\0")
    dirty = dirty or any(entry[:1].islower() or entry[:1].upper() == b"S"
                         for entry in flagged if entry)
    dirty = dirty or any(name in untracked for name in names)
    return {
        "schema_version": 1,
        "source": {
            "revision": revision, "dirty": dirty,
            "inputs_sha256": hashlib.sha256(encoded(inputs)).hexdigest(),
            "input_files": len(inputs),
            "coverage": "project build inputs; external dependencies identified by pinned inputs",
        },
        "sdkconfig_sha256": sha256(build_dir / "sdkconfig"),
    }


def write_json(path: Path, value: dict) -> None:
    write_if_changed(path, json.dumps(value, sort_keys=True, indent=2) + "\n")


def write_if_changed(path: Path, text: str) -> None:
    if path.is_file() and path.read_text() == text:
        return
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text)
    temporary.replace(path)


def marker(value: dict) -> bytes:
    return (MARKER + hashlib.sha256(encoded(value)).hexdigest()).encode() + b"\0"


def capture(project_root: Path, build_dir: Path) -> None:
    value = snapshot(project_root, build_dir)
    write_json(build_dir / SNAPSHOT, value)
    literal = json.dumps(marker(value).decode().rstrip("\0"))
    write_if_changed(build_dir / GENERATED_C,
                     "/* SPDX-License-Identifier: Apache-2.0 */\n"
                     "/* Generated build identity; linked and retained by the application. */\n"
                     "const char mybot_build_source_sha256[] __attribute__((used)) = " + literal + ";\n")


def build_file(build_dir: Path, name: str) -> Path:
    if Path(name).is_absolute() or ".." in Path(name).parts:
        raise RuntimeError(f"invalid build provenance artifact: {name}")
    path = (build_dir / name).resolve()
    if build_dir not in path.parents or not path.is_file():
        raise RuntimeError(f"invalid build provenance artifact: {name}")
    return path


def artifact_names(build_dir: Path) -> tuple[dict, list[str]]:
    description = json.loads((build_dir / "project_description.json").read_text())
    flash = json.loads((build_dir / "flasher_args.json").read_text())
    names = {"sdkconfig", "project_description.json", "flasher_args.json",
             description["app_elf"], description["app_bin"], *flash["flash_files"].values()}
    return description, sorted(names)


def verify_embedded(build_dir: Path, description: dict, value: dict) -> None:
    expected = marker(value)
    for name in (description["app_elf"], description["app_bin"]):
        if expected not in build_file(build_dir, name).read_bytes():
            raise RuntimeError(f"build provenance is not embedded in {name}; rebuild the application")


def finalize(project_root: Path, build_dir: Path) -> dict:
    value = json.loads((build_dir / SNAPSHOT).read_text())
    if value != snapshot(project_root, build_dir):
        raise RuntimeError("source or configuration changed during the build; rebuild before packaging")
    description, names = artifact_names(build_dir)
    verify_embedded(build_dir, description, value)
    artifacts = [{"file": name, "sha256": sha256(build_file(build_dir, name))} for name in names]
    stamp = {"schema_version": 1, "snapshot": value, "artifacts": artifacts}
    write_json(build_dir / STAMP, stamp)
    return stamp


def verify(build_dir: Path) -> dict:
    path = build_file(build_dir, STAMP)
    stamp = json.loads(path.read_text())
    if stamp.get("schema_version") != 1 or not isinstance(stamp.get("snapshot"), dict):
        raise RuntimeError("invalid build provenance stamp; rebuild the application")
    value = stamp["snapshot"]
    source = value.get("source", {})
    if (not isinstance(source, dict) or value.get("schema_version") != 1 or
            type(source.get("dirty")) is not bool or
            not isinstance(source.get("revision"), str) or
            not re.fullmatch(r"[0-9a-f]{40}", source["revision"]) or
            not isinstance(source.get("inputs_sha256"), str) or
            not re.fullmatch(r"[0-9a-f]{64}", source["inputs_sha256"]) or
            type(source.get("input_files")) is not int or source["input_files"] < 1 or
            not isinstance(source.get("coverage"), str)):
        raise RuntimeError("invalid build source snapshot; rebuild the application")
    description, names = artifact_names(build_dir)
    artifacts = stamp.get("artifacts", [])
    if (not isinstance(artifacts, list) or any(not isinstance(item, dict) for item in artifacts) or
            [item.get("file") for item in artifacts] != names):
        raise RuntimeError("build provenance artifact set does not match the build; rebuild")
    for item in artifacts:
        if item.get("sha256") != sha256(build_file(build_dir, item["file"])):
            raise RuntimeError(f"build provenance artifact mismatch: {item['file']}; rebuild")
    if value.get("sdkconfig_sha256") != sha256(build_file(build_dir, "sdkconfig")):
        raise RuntimeError("build provenance configuration mismatch; rebuild")
    verify_embedded(build_dir, description, value)
    return stamp


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("capture", "finalize"))
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    project_root, build_dir = args.project_root.resolve(), args.build_dir.resolve()
    if args.phase == "capture":
        capture(project_root, build_dir)
    else:
        finalize(project_root, build_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"build provenance: {error}")
