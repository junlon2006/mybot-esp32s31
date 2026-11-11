#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and validate ESP-Mosaico V1.2 firmware variants.

Both languages use 60 ms audio without video. Local builds reuse build/mosaico-v1.2
and preserve existing build/release contents. An explicit --build-root selects
isolated CI build/sdkconfig directories with ownership-checked cleanup. Verified
images and metadata are written to releases/ by default. Python 3.10 is supported.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
RELEASES_DIR = PROJECT_ROOT / "releases"
GENERATED_ROOT_MARKER = ".mybot-generated-root.json"
DEFAULT_BOARD = "esp-mosaico-v1.2"


def format_elapsed(seconds: float) -> str:
    total = max(0, int(seconds))
    hours, remainder = divmod(total, 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


def idf_command() -> str:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError("ESP-IDF is not sourced; run source /path/to/esp-idf/export.sh first")
    idf = shutil.which("idf.py")
    if not idf:
        raise RuntimeError("idf.py is not in PATH; source the ESP-IDF export script first")
    version = subprocess.run(
        [idf, "--preview", "--version"], check=True, capture_output=True, text=True
    ).stdout.strip()
    if version != "ESP-IDF v6.1":
        raise RuntimeError(f"expected ESP-IDF v6.1, got {version}")
    return idf


def run(idf: str, args: list[str], dry_run: bool) -> None:
    command = [idf, "--preview", *args]
    print(">>>", shlex.join(command))
    if dry_run:
        return
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)


def normalize_generated_root(path: Path, label: str) -> Path:
    """Reject unsafe paths before normalization can hide links or parent traversal."""
    path = path.expanduser()
    if ".." in path.parts:
        raise RuntimeError(f"refusing parent traversal in {label} path: {path}")
    path = Path(os.path.abspath(path))
    if path.anchor != "/":
        raise RuntimeError(f"refusing non-canonical {label} path: {path}")
    for component in (path, *path.parents):
        if component.is_symlink():
            raise RuntimeError(f"refusing symlink in {label} path: {component}")
    protected = {Path(path.anchor), Path.home().resolve(), PROJECT_ROOT, *PROJECT_ROOT.parents}
    if path in protected:
        raise RuntimeError(f"refusing unsafe {label} path: {path}")
    if PROJECT_ROOT in path.parents and not any(
        path == allowed or allowed in path.parents
        for allowed in (PROJECT_ROOT / "build", RELEASES_DIR)
    ):
        raise RuntimeError(f"{label} path must not be inside the project source tree: {path}")
    return path


def _open_directory(path: Path, *, create: bool = False) -> int:
    """Walk from the filesystem root without following any symlink component."""
    if not hasattr(os, "O_NOFOLLOW") or not hasattr(os, "O_DIRECTORY"):
        raise RuntimeError("safe output directory handling requires POSIX directory descriptors")
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    directory = os.open(path.anchor, flags)
    try:
        for component in path.parts[1:]:
            try:
                child = os.open(component, flags, dir_fd=directory)
            except FileNotFoundError:
                if not create:
                    raise
                os.mkdir(component, dir_fd=directory)
                child = os.open(component, flags, dir_fd=directory)
            os.close(directory)
            directory = child
        return directory
    except BaseException:
        os.close(directory)
        raise


def _root_identity(path: Path, label: str) -> dict[str, str | int]:
    return {"format": 1, "project": str(PROJECT_ROOT), "root": str(path), "role": label}


def _validate_root_contents(directory: int, path: Path, label: str) -> bool:
    """An existing nonempty root must already carry this script's ownership marker."""
    if not os.listdir(directory):
        return False
    try:
        marker = os.open(
            GENERATED_ROOT_MARKER, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory
        )
        with os.fdopen(marker, "r", encoding="utf-8") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > 4096:
                raise ValueError("marker is not a small regular file")
            identity = json.loads(stream.read(4097))
        if (
            not isinstance(identity, dict)
            or type(identity.get("format")) is not int
            or identity != _root_identity(path, label)
        ):
            raise ValueError("marker does not match this project, directory and role")
    except (OSError, ValueError) as error:
        raise RuntimeError(
            f"refusing unowned or invalid {label} directory: {path}; "
            "choose a new/empty dedicated directory, or move the old directory aside "
            "after inspecting its contents"
        ) from error
    return True


def validate_generated_root(path: Path, label: str) -> Path:
    path = normalize_generated_root(path, label)
    try:
        directory = _open_directory(path)
    except FileNotFoundError:
        return path
    except OSError as error:
        raise RuntimeError(f"refusing invalid {label} directory: {path}") from error
    try:
        _validate_root_contents(directory, path, label)
    finally:
        os.close(directory)
    return path


def validate_output_roots(build_root: Path, output_root: Path) -> tuple[Path, Path]:
    """Preflight both roots before either one can be created or cleaned."""
    build_root = validate_generated_root(build_root, "build")
    output_root = validate_generated_root(output_root, "output")
    if (
        build_root == output_root
        or build_root in output_root.parents
        or output_root in build_root.parents
    ):
        raise RuntimeError("build and output roots must be separate, non-overlapping directories")
    return build_root, output_root


_CLEANUP_DIR_FD_OPERATIONS = (os.open, os.mkdir, os.stat, os.unlink, os.rmdir)
_CLEANUP_LIST_DIRECTORY = os.listdir
_CLEANUP_STAT = os.stat


def _supports_safe_cleanup() -> bool:
    """Require public descriptor APIs, including those available on POSIX Python 3.10."""
    return (
        hasattr(os, "O_DIRECTORY")
        and hasattr(os, "O_NOFOLLOW")
        and all(operation in os.supports_dir_fd for operation in _CLEANUP_DIR_FD_OPERATIONS)
        and _CLEANUP_LIST_DIRECTORY in os.supports_fd
        and _CLEANUP_STAT in os.supports_follow_symlinks
    )


def _remove_generated_entry(directory: int, name: str) -> None:
    """Remove one child without resolving paths outside its already-open parent."""
    original = os.stat(name, dir_fd=directory, follow_symlinks=False)
    if not stat.S_ISDIR(original.st_mode):
        os.unlink(name, dir_fd=directory)
        return

    child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
    try:
        opened = os.fstat(child)
        if not os.path.samestat(original, opened):
            raise RuntimeError(f"refusing directory replaced during cleanup: {name}")
        for entry in os.listdir(child):
            _remove_generated_entry(child, entry)
        current = os.stat(name, dir_fd=directory, follow_symlinks=False)
        if not os.path.samestat(current, opened):
            raise RuntimeError(f"refusing directory replaced during cleanup: {name}")
        os.rmdir(name, dir_fd=directory)
    finally:
        os.close(child)


def prepare_generated_root(path: Path, label: str, dry_run: bool, *, clean: bool = True) -> None:
    """Only remove children of an owned root, using its descriptor as the deletion anchor."""
    path = validate_generated_root(path, label)
    action = ("Would clean" if clean else "Would prepare") if dry_run else (
        "Cleaning" if clean else "Preparing"
    )
    print(f"{action} {label}: {path}")
    if dry_run:
        return
    if clean and not _supports_safe_cleanup():
        raise RuntimeError("safe cleanup requires POSIX directory descriptors and no-follow operations")
    directory = _open_directory(path, create=True)
    try:
        owned = _validate_root_contents(directory, path, label)
        if not owned:
            marker = os.open(
                GENERATED_ROOT_MARKER,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                0o600,
                dir_fd=directory,
            )
            with os.fdopen(marker, "w", encoding="utf-8") as stream:
                json.dump(_root_identity(path, label), stream)
                stream.write("\n")
        if clean:
            for name in os.listdir(directory):
                if name == GENERATED_ROOT_MARKER:
                    continue
                _remove_generated_entry(directory, name)
    finally:
        os.close(directory)


def validate_writable_generated_file(path: Path, label: str) -> None:
    """An existing generated-file name must not redirect writes to another file."""
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise RuntimeError(f"refusing non-regular or linked {label}: {path}")


def validate_local_roots(output_root: Path) -> tuple[Path, Path]:
    """Local IDF outputs share one fixed directory; neither root is purged or claimed."""
    build_dir = normalize_generated_root(PROJECT_ROOT / "build/mosaico-v1.2", "build")
    output_root = normalize_generated_root(output_root, "output")
    if build_dir == output_root or build_dir in output_root.parents or output_root in build_dir.parents:
        raise RuntimeError("build and output roots must be separate, non-overlapping directories")
    for path in (build_dir, output_root):
        try:
            directory = _open_directory(path)
        except FileNotFoundError:
            continue
        os.close(directory)
    for name in ("sdkconfig", "sdkconfig.old"):
        validate_writable_generated_file(build_dir / name, "configuration file")
    return build_dir, output_root


def prepare_local_directory(path: Path, label: str, dry_run: bool) -> None:
    print(f"{'Would reuse' if dry_run else 'Reusing'} {label}: {path}")
    if not dry_run:
        directory = _open_directory(path, create=True)
        os.close(directory)


def configure_local_variant(
    idf: str, base_args: list[str], sdkconfig: Path, language: str, aec_mode: str, dry_run: bool,
) -> None:
    """Use IDF's configuration server to switch choices while retaining other settings."""
    aec_symbols = {
        "local": "MYBOT_LOCAL_AEC",
        "cloud": "MYBOT_CLOUD_AEC",
        "disabled": "MYBOT_AEC_DISABLED",
    }
    language_symbol = "MYBOT_LANGUAGE_ZH_CN" if language == "zh-CN" else "MYBOT_LANGUAGE_EN_US"
    request = {
        "version": 2,
        "set": {language_symbol: True, aec_symbols[aec_mode]: True, "MYBOT_AUDIO_PTIME_60": True},
        "save": None,
    }
    command = [idf, "--preview", *base_args, "confserver", "--buffer-size", "4096"]
    print(">>>", shlex.join(command))
    print("    IDF configuration:", language, f"AEC={aec_mode}", "60 ms")
    if dry_run:
        return
    try:
        result = subprocess.run(
            command, cwd=PROJECT_ROOT, input=json.dumps(request) + "\n",
            capture_output=True, text=True, check=True,
        )
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or "").strip()[-2000:]
        if not detail:
            detail = f"exit {error.returncode}; see {sdkconfig.parent / 'log'}"
        raise RuntimeError(f"ESP-IDF configuration server failed: {detail}") from error
    responses = []
    for line in result.stdout.splitlines():
        try:
            response = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(response, dict) and "version" in response:
            responses.append(response)
    if not responses or responses[-1].get("version") != request["version"]:
        raise RuntimeError("ESP-IDF configuration server returned no configuration acknowledgement")
    for response in responses:
        if response.get("error"):
            raise RuntimeError(f"ESP-IDF configuration server rejected settings: {response['error']}")
    config = dict(re.findall(r"^(CONFIG_\w+)=(.*)$", sdkconfig.read_text(), re.MULTILINE))
    expected = {
        "CONFIG_MYBOT_LANGUAGE_TAG": json.dumps(language),
        "CONFIG_MYBOT_AUDIO_PTIME_MS": "60",
        "CONFIG_" + aec_symbols[aec_mode]: "y",
    }
    if any(config.get(key) != value for key, value in expected.items()) or any(
        config.get("CONFIG_" + symbol) == "y"
        for mode, symbol in aec_symbols.items() if mode != aec_mode
    ):
        raise RuntimeError("ESP-IDF did not save the requested language, AEC mode and 60 ms configuration")


def validate_variant_outputs(output_root: Path, name: str) -> None:
    """Reject output aliases before merge-bin or the metadata writer can follow them."""
    for suffix in (".bin", ".manifest.json", ".sha256", ".flash.txt", ".licenses.txt"):
        validate_writable_generated_file(output_root / (name + suffix), "firmware output")


def build_variant(
    idf: str,
    language: str,
    build_root: Path,
    output_root: Path,
    dry_run: bool,
    aec_mode: str = "local",
    *,
    local: bool = False,
) -> Path:
    started_at = time.perf_counter()
    name = f"{DEFAULT_BOARD}-{language}"
    build_dir = build_root if local else build_root / f"build-{name}"
    sdkconfig = build_dir / "sdkconfig"
    output_path = output_root / f"{name}.bin"
    defaults = ["sdkconfig.defaults", "ci/ptime60.defaults", f"ci/{aec_mode}-aec.defaults"]
    if language == "en-US":
        defaults.append("ci/en-us.defaults")
    defaults_value = ";".join(defaults)

    base_args = [
        "-B",
        str(build_dir),
        "-DMYBOT_MOSAICO_UAC_TEST=OFF",
        f"-DSDKCONFIG={sdkconfig}",
        f"-DSDKCONFIG_DEFAULTS={defaults_value}",
    ]
    # Local stages reuse the cache established by reconfigure. Repeating -D
    # options would make idf.py reconfigure before every subsequent operation.
    execution_args = ["-B", str(build_dir)] if local else base_args
    print(f"\nBuilding {name} (60 ms audio, {aec_mode} AEC)")
    try:
        validate_variant_outputs(output_root, name)
        run(idf, [*base_args, "reconfigure"], dry_run)
        if local:
            configure_local_variant(idf, execution_args, sdkconfig, language, aec_mode, dry_run)
        run(idf, [*execution_args, "build"], dry_run)
        run(idf, [*execution_args, "size"], dry_run)
        # idf.py invokes merge-bin from build_dir: -o must be absolute.
        run(idf, [*execution_args, "merge-bin", "-o", str(output_path)], dry_run)
        command = [
            sys.executable, str(PROJECT_ROOT / "scripts/check_s31_build.py"),
            "--build-dir", str(build_dir), "--language", language,
            "--aec-mode", aec_mode,
            "--merged", str(output_path),
        ]
        print(">>>", shlex.join(command))
        if not dry_run:
            subprocess.run(command, cwd=PROJECT_ROOT, check=True)
    except Exception:
        print(f"[FAIL] {name} elapsed={format_elapsed(time.perf_counter() - started_at)}")
        raise
    result = "DRY-RUN" if dry_run else "OK"
    print(f"[{result}] {name} elapsed={format_elapsed(time.perf_counter() - started_at)}")
    return output_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--aec-mode", choices=("local", "cloud", "disabled"), default="local",
        help="firmware AEC mode (default: local)",
    )
    parser.add_argument(
        "--language",
        choices=("zh-CN", "en-US", "both"),
        default="both",
        help="firmware language (default: both)",
    )
    parser.add_argument(
        "--build-root",
        type=Path,
        default=None,
        help="isolated CI root with ownership-checked cleanup (default: reuse build/mosaico-v1.2)",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=RELEASES_DIR,
        help="firmware output root (default: repository releases/; local builds preserve other files)",
    )
    parser.add_argument("--dry-run", action="store_true", help="print commands without building")
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="keep isolated CI roots; local builds always preserve existing directories",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    languages = ("zh-CN", "en-US") if args.language == "both" else (args.language,)

    local = args.build_root is None
    build_root, output_root = (
        validate_local_roots(args.output_root) if local
        else validate_output_roots(args.build_root, args.output_root)
    )
    idf = idf_command()
    if local:
        prepare_local_directory(build_root, "build", args.dry_run)
        prepare_local_directory(output_root, "output", args.dry_run)
    else:
        prepare_generated_root(build_root, "build", args.dry_run, clean=not args.no_clean)
        prepare_generated_root(output_root, "output", args.dry_run, clean=not args.no_clean)

    outputs: list[Path] = []
    batch_started_at = time.perf_counter()
    try:
        for language in languages:
            if local:
                outputs.append(build_variant(
                    idf, language, build_root, output_root, args.dry_run, args.aec_mode, local=True,
                ))
            else:
                outputs.append(build_variant(idf, language, build_root, output_root, args.dry_run, args.aec_mode))
    finally:
        print(f"\nTotal elapsed: {format_elapsed(time.perf_counter() - batch_started_at)}")

    print(f"\nPlanned variants: {len(outputs)}")
    print(f"Output directory: {output_root}")
    for output in outputs:
        print(f"  {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
