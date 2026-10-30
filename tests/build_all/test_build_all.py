#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host regression tests for board selection, preview commands and safe outputs."""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "build_all.py"
SPEC = importlib.util.spec_from_file_location("mybot_build_all_under_test", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
builder = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = builder
SPEC.loader.exec_module(builder)

MARKER = ".mybot-generated-root.json"
SAFE_CLEANUP = sys.version_info >= (3, 11) and builder.shutil.rmtree.avoids_symlink_attacks


class BoardCapabilityTests(unittest.TestCase):
    def test_s31_profile_declares_display_and_no_video(self) -> None:
        profiles = {profile.name: profile for profile in builder.supported_boards()}
        profile = profiles["esp32-s31-korvo-1"]
        self.assertTrue(profile.has_display)
        self.assertFalse(profile.video_capable)

    def test_explicit_false_overrides_legacy_source_detection(self) -> None:
        profile = """
set(MYBOT_BOARD_HAS_DISPLAY OFF)
set(MYBOT_BOARD_HAS_VIDEO OFF)
mybot_display_add_panel(...)
set(MYBOT_BOARD_SOURCES cores3_camera_video.c)
"""
        self.assertFalse(builder.board_capability(profile, "MYBOT_BOARD_HAS_DISPLAY", True))
        self.assertFalse(builder.board_capability(profile, "MYBOT_BOARD_HAS_VIDEO", True))

    def test_explicit_capabilities_do_not_depend_on_driver_names(self) -> None:
        profile = """
set(MYBOT_BOARD_HAS_DISPLAY ON)
set(MYBOT_BOARD_HAS_VIDEO TRUE)
set(MYBOT_BOARD_SOURCES another_board_camera.c)
"""
        self.assertTrue(builder.board_capability(profile, "MYBOT_BOARD_HAS_DISPLAY", False))
        self.assertTrue(builder.board_capability(profile, "MYBOT_BOARD_HAS_VIDEO", False))

    def test_old_profiles_keep_fallback_and_commented_values_do_not_override(self) -> None:
        profile = "# set(MYBOT_BOARD_HAS_VIDEO OFF)\nset(MYBOT_BOARD_SOURCES old_camera.c)"
        self.assertTrue(builder.board_capability(profile, "MYBOT_BOARD_HAS_VIDEO", True))
        self.assertFalse(builder.board_capability(profile, "MYBOT_BOARD_HAS_VIDEO", False))

    def test_invalid_explicit_capability_fails_closed(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "literal CMake boolean"):
            builder.board_capability(
                "set(MYBOT_BOARD_HAS_VIDEO ${OTHER_BOARD_VIDEO})", "MYBOT_BOARD_HAS_VIDEO", True
            )

    def test_every_executed_idf_command_uses_preview(self) -> None:
        with (
            mock.patch.dict(builder.os.environ, {"IDF_PATH": "/test/idf"}),
            mock.patch.object(builder.shutil, "which", return_value="/test/idf.py"),
            mock.patch.object(builder.subprocess, "run") as execute,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            execute.return_value.stdout = "ESP-IDF v6.1\n"
            idf = builder.idf_command()
            builder.run(idf, ["-B", "build/s31", "build"], False)
        commands = [call.args[0] for call in execute.call_args_list]
        self.assertEqual(commands, [
            ["/test/idf.py", "--preview", "--version"],
            ["/test/idf.py", "--preview", "-B", "build/s31", "build"],
        ])


class S31DryRunTests(unittest.TestCase):
    def test_both_languages_have_no_video_and_leave_outputs_untouched(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mybot-s31-dry-run-") as temporary:
            workspace = Path(temporary).resolve()
            build = workspace / "build"
            output = workspace / "releases"
            build.mkdir()
            marker = {
                "format": 1, "project": str(builder.PROJECT_ROOT),
                "root": str(build), "role": "build",
            }
            (build / MARKER).write_text(json.dumps(marker), encoding="utf-8")
            sentinel = build / "existing-firmware.bin"
            sentinel.write_bytes(b"preserve")
            args = argparse.Namespace(
                board=["esp32-s31-korvo-1"], language="both", video="auto",
                build_root=build, output_root=output, dry_run=True, no_clean=False,
            )
            printed = io.StringIO()
            with (
                mock.patch.object(builder, "parse_args", return_value=args),
                mock.patch.object(builder, "idf_command", return_value="idf.py"),
                mock.patch.object(builder.subprocess, "run") as execute,
                mock.patch.object(builder.shutil, "rmtree") as remove,
                contextlib.redirect_stdout(printed),
            ):
                self.assertEqual(builder.main(), 0)
            execute.assert_not_called()
            remove.assert_not_called()
            self.assertEqual(sentinel.read_bytes(), b"preserve")
            self.assertEqual({path.name for path in build.iterdir()}, {MARKER, sentinel.name})
            self.assertFalse(output.exists())
            commands = [line for line in printed.getvalue().splitlines() if line.startswith(">>> idf.py")]
            self.assertEqual(len(commands), 8)
            self.assertTrue(all(line.startswith(">>> idf.py --preview ") for line in commands))
            self.assertFalse(any("ci/video.defaults" in line for line in commands))
            self.assertEqual(sum("ci/en-us.defaults" in line for line in commands), 4)
            self.assertIn("display=yes, video=no", printed.getvalue())
            self.assertIn("esp32-s31-korvo-1-zh-CN.bin", printed.getvalue())
            self.assertIn("esp32-s31-korvo-1-en-US.bin", printed.getvalue())
            packaging = [line for line in printed.getvalue().splitlines() if "check_s31_build.py" in line]
            self.assertEqual(len(packaging), 2)
            for language in ("zh-CN", "en-US"):
                image = str(output / f"esp32-s31-korvo-1-{language}.bin")
                self.assertTrue(any(f"merge-bin -o {image}" in line for line in commands))
                self.assertTrue(any(f"--merged {image}" in line for line in packaging))

    def test_video_on_is_rejected_before_output_preparation_or_idf(self) -> None:
        args = argparse.Namespace(board=["esp32-s31-korvo-1"], video="on", language="both")
        with (
            mock.patch.object(builder, "parse_args", return_value=args),
            mock.patch.object(builder, "prepare_generated_root") as prepare,
            mock.patch.object(builder, "idf_command") as idf,
            self.assertRaisesRegex(RuntimeError, "video is unsupported"),
        ):
            builder.main()
        prepare.assert_not_called()
        idf.assert_not_called()


class GeneratedRootTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="mybot-build-safety-")
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name).resolve()
        self.project = self.workspace / "repository"
        self.project.mkdir()
        patch = mock.patch.object(builder, "PROJECT_ROOT", self.project)
        patch.start()
        self.addCleanup(patch.stop)
        releases_patch = mock.patch.object(builder, "RELEASES_DIR", self.project / "releases")
        releases_patch.start()
        self.addCleanup(releases_patch.stop)
        self.output = io.StringIO()
        redirect = contextlib.redirect_stdout(self.output)
        redirect.__enter__()
        self.addCleanup(redirect.__exit__, None, None, None)

    def marker_data(self, path: Path, role: str = "build") -> dict[str, object]:
        return {
            "format": 1,
            "project": str(self.project),
            "root": str(path),
            "role": role,
        }

    def mark(self, path: Path, role: str = "build") -> None:
        path.mkdir(parents=True, exist_ok=True)
        (path / MARKER).write_text(json.dumps(self.marker_data(path, role)), encoding="utf-8")

    def args(self, build: Path, output: Path, **changes: object) -> argparse.Namespace:
        values = {
            "board": None,
            "language": "en-US",
            "video": "off",
            "build_root": build,
            "output_root": output,
            "dry_run": False,
            "no_clean": False,
        }
        values.update(changes)
        return argparse.Namespace(**values)

    def run_main(self, args: argparse.Namespace) -> tuple[int, mock.Mock, mock.Mock]:
        profile = builder.BoardProfile("test-board", False, False)
        with (
            mock.patch.object(builder, "parse_args", return_value=args),
            mock.patch.object(builder, "supported_boards", return_value=[profile]),
            mock.patch.object(builder, "idf_command", return_value="idf.py") as idf,
            mock.patch.object(builder, "build_variant", return_value=args.output_root / "test.bin") as build,
        ):
            result = builder.main()
        return result, idf, build

    def test_default_and_custom_empty_roots_are_accepted(self) -> None:
        for path in (self.project / "build", self.project / "releases", self.workspace / "custom"):
            with self.subTest(path=path):
                self.assertEqual(builder.normalize_generated_root(path, "build"), path)
                builder.validate_generated_root(path, "build")
                path.mkdir()
                builder.validate_generated_root(path, "build")

    def test_project_and_every_workspace_ancestor_are_rejected(self) -> None:
        sentinel = self.workspace / "workspace-source.txt"
        sentinel.write_text("preserve", encoding="utf-8")
        with mock.patch.object(builder.shutil, "rmtree") as remove:
            for path in (self.project, self.workspace, *self.workspace.parents):
                with self.subTest(path=path), self.assertRaises(RuntimeError):
                    builder.normalize_generated_root(path, "output")
            remove.assert_not_called()
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")

    def test_home_is_rejected_without_deletion(self) -> None:
        fake_home = self.workspace / "home"
        fake_home.mkdir()
        with (
            mock.patch.object(builder.Path, "home", return_value=fake_home),
            mock.patch.object(builder.shutil, "rmtree") as remove,
            self.assertRaises(RuntimeError),
        ):
            builder.normalize_generated_root(fake_home, "build")
        remove.assert_not_called()

    def test_source_directories_in_project_are_rejected(self) -> None:
        for suffix in ("main", "components", "scripts", "docs", "build-other", "releases-old"):
            path = self.project / suffix
            path.mkdir()
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                builder.normalize_generated_root(path, "build")

    def test_nested_generated_directories_are_accepted(self) -> None:
        for path in (self.project / "build" / "batch", self.project / "releases" / "batch"):
            with self.subTest(path=path):
                self.assertEqual(builder.normalize_generated_root(path, "build"), path)

    def test_parent_traversal_is_rejected_before_normalization(self) -> None:
        for path in (self.project / "build" / "..", self.project / ".." / "custom"):
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                builder.normalize_generated_root(path, "build")

    def test_double_slash_cannot_alias_protected_or_source_directories(self) -> None:
        for original in (self.workspace, self.project, self.project / "main"):
            alias = Path("//" + str(original).lstrip("/"))
            with self.subTest(path=alias), self.assertRaises(RuntimeError):
                builder.normalize_generated_root(alias, "build")

    def test_root_symlinks_and_symlink_ancestors_are_rejected(self) -> None:
        target = self.workspace / "target"
        target.mkdir()
        link = self.workspace / "link"
        link.symlink_to(target, target_is_directory=True)
        broken = self.workspace / "broken"
        broken.symlink_to(self.workspace / "missing", target_is_directory=True)
        for path in (link, link / "child", link / ".." / "other", broken, broken / "child"):
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                builder.normalize_generated_root(path, "build")

    def test_file_in_place_of_root_is_rejected(self) -> None:
        path = self.workspace / "file"
        path.write_text("preserve", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            builder.validate_generated_root(path, "build")
        self.assertEqual(path.read_text(encoding="utf-8"), "preserve")

    def test_nonempty_unmarked_root_is_rejected(self) -> None:
        path = self.workspace / "unowned"
        path.mkdir()
        sentinel = path / "source.txt"
        sentinel.write_text("preserve", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            builder.validate_generated_root(path, "build")
        self.assertTrue(sentinel.exists())

    def test_valid_ownership_marker_is_accepted(self) -> None:
        path = self.workspace / "owned"
        self.mark(path)
        (path / "artifact.bin").write_bytes(b"firmware")
        builder.validate_generated_root(path, "build")

    def test_invalid_marker_contents_are_rejected(self) -> None:
        path = self.workspace / "bad-marker"
        path.mkdir()
        marker = path / MARKER
        invalid = ["not-json", "null", "[]", "{}", json.dumps({"format": 1})]
        for value in invalid:
            with self.subTest(value=value):
                marker.write_text(value, encoding="utf-8")
                with self.assertRaises(RuntimeError):
                    builder.validate_generated_root(path, "build")

    def test_wrong_marker_identity_is_rejected(self) -> None:
        path = self.workspace / "wrong-owner"
        path.mkdir()
        changes = (
            {"format": 2},
            {"format": True},
            {"format": 1.0},
            {"project": str(self.workspace / "another-project")},
            {"root": str(self.workspace / "another-root")},
            {"role": "output"},
        )
        for change in changes:
            with self.subTest(change=change):
                data = self.marker_data(path)
                data.update(change)
                (path / MARKER).write_text(json.dumps(data), encoding="utf-8")
                with self.assertRaises(RuntimeError):
                    builder.validate_generated_root(path, "build")

    def test_marker_symlink_is_rejected(self) -> None:
        path = self.workspace / "marker-link"
        path.mkdir()
        target = self.workspace / "marker-target.json"
        target.write_text(json.dumps(self.marker_data(path)), encoding="utf-8")
        (path / MARKER).symlink_to(target)
        with self.assertRaises(RuntimeError):
            builder.validate_generated_root(path, "build")
        self.assertTrue(target.exists())

    def test_directory_and_oversized_markers_are_rejected(self) -> None:
        directory_root = self.workspace / "directory-marker"
        directory_root.mkdir()
        (directory_root / MARKER).mkdir()
        with self.assertRaises(RuntimeError):
            builder.validate_generated_root(directory_root, "build")
        oversized_root = self.workspace / "oversized-marker"
        oversized_root.mkdir()
        (oversized_root / MARKER).write_text(" " * 8192, encoding="utf-8")
        with self.assertRaises(RuntimeError):
            builder.validate_generated_root(oversized_root, "build")

    def test_equal_and_nested_roots_are_rejected(self) -> None:
        root = self.workspace / "generated"
        for build, output in ((root, root), (root, root / "releases"), (root / "build", root)):
            with self.subTest(build=build, output=output), self.assertRaises(RuntimeError):
                builder.validate_output_roots(build, output)

    def test_double_slash_cannot_hide_equal_or_overlapping_roots(self) -> None:
        root = self.workspace / "generated"
        alias = Path("//" + str(root).lstrip("/"))
        for build, output in ((root, alias), (root, alias / "releases"), (alias / "build", root)):
            with self.subTest(build=build, output=output), self.assertRaises(RuntimeError):
                builder.validate_output_roots(build, output)

    def test_distinct_roots_are_normalized_and_accepted(self) -> None:
        build = self.project / "build"
        output = self.workspace / "firmware"
        self.assertEqual(builder.validate_output_roots(build, output), (build, output))

    @unittest.skipUnless(SAFE_CLEANUP, "cleanup requires Python 3.11+ and symlink-safe rmtree")
    def test_prepare_new_root_writes_ownership_marker(self) -> None:
        path = self.workspace / "new" / "build"
        builder.prepare_generated_root(path, "build", False)
        self.assertEqual(json.loads((path / MARKER).read_text(encoding="utf-8")), self.marker_data(path))

    @unittest.skipUnless(SAFE_CLEANUP, "cleanup requires Python 3.11+ and symlink-safe rmtree")
    def test_owned_cleanup_keeps_root_and_marker_and_can_repeat(self) -> None:
        path = self.workspace / "owned-cleanup"
        self.mark(path)
        inode = path.stat().st_ino
        (path / "firmware.bin").write_bytes(b"firmware")
        nested = path / "build-board" / "objects"
        nested.mkdir(parents=True)
        (nested / "object.o").write_bytes(b"object")
        builder.prepare_generated_root(path, "build", False)
        self.assertEqual(path.stat().st_ino, inode)
        self.assertEqual({entry.name for entry in path.iterdir()}, {MARKER})
        builder.prepare_generated_root(path, "build", False)
        self.assertEqual(path.stat().st_ino, inode)
        self.assertEqual(json.loads((path / MARKER).read_text(encoding="utf-8")), self.marker_data(path))

    @unittest.skipUnless(SAFE_CLEANUP, "cleanup requires Python 3.11+ and symlink-safe rmtree")
    def test_symlink_children_are_removed_without_following(self) -> None:
        path = self.workspace / "owned-links"
        self.mark(path)
        external = self.workspace / "external-data"
        external.mkdir()
        sentinel = external / "source.txt"
        sentinel.write_text("preserve", encoding="utf-8")
        (path / "directory-link").symlink_to(external, target_is_directory=True)
        (path / "file-link").symlink_to(sentinel)
        builder.prepare_generated_root(path, "build", False)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")
        self.assertEqual({entry.name for entry in path.iterdir()}, {MARKER})

    @unittest.skipUnless(SAFE_CLEANUP, "cleanup requires Python 3.11+ and symlink-safe rmtree")
    def test_root_symlink_swap_after_validation_cannot_delete_target(self) -> None:
        path = self.workspace / "swap-root"
        self.mark(path)
        artifact = path / "object.o"
        artifact.write_bytes(b"preserve artifact")
        moved = self.workspace / "moved-root"
        external = self.workspace / "external-source"
        external.mkdir()
        sentinel = external / "source.txt"
        sentinel.write_text("preserve source", encoding="utf-8")
        original_open = builder._open_directory

        def swap_before_mutation(selected: Path, *, create: bool = False) -> int:
            if create:
                selected.rename(moved)
                selected.symlink_to(external, target_is_directory=True)
            return original_open(selected, create=create)

        with (
            mock.patch.object(builder, "_open_directory", side_effect=swap_before_mutation),
            self.assertRaises((OSError, RuntimeError)),
        ):
            builder.prepare_generated_root(path, "build", False)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve source")
        self.assertEqual((moved / artifact.name).read_bytes(), b"preserve artifact")
        self.assertFalse((external / MARKER).exists())

    def test_no_clean_preserves_owned_content(self) -> None:
        path = self.workspace / "incremental"
        self.mark(path)
        artifact = path / "object.o"
        artifact.write_bytes(b"keep")
        builder.prepare_generated_root(path, "build", False, clean=False)
        self.assertEqual(artifact.read_bytes(), b"keep")

    def test_no_clean_claims_new_dedicated_directory(self) -> None:
        path = self.workspace / "new-incremental"
        builder.prepare_generated_root(path, "build", False, clean=False)
        self.assertEqual(json.loads((path / MARKER).read_text(encoding="utf-8")), self.marker_data(path))

    def test_cleanup_fails_closed_without_symlink_safe_rmtree(self) -> None:
        path = self.workspace / "unsupported-cleanup"
        self.mark(path)
        artifact = path / "firmware.bin"
        artifact.write_bytes(b"preserve")
        with (
            mock.patch.object(builder.shutil.rmtree, "avoids_symlink_attacks", False),
            self.assertRaises(RuntimeError),
        ):
            builder.prepare_generated_root(path, "build", False)
        self.assertEqual(artifact.read_bytes(), b"preserve")

    def test_cleanup_fails_closed_on_python_310_before_mutation(self) -> None:
        path = self.workspace / "unsupported-python"
        self.mark(path)
        artifact = path / "firmware.bin"
        artifact.write_bytes(b"preserve")
        with (
            mock.patch.object(builder.sys, "version_info", (3, 10)),
            mock.patch.object(builder.shutil, "rmtree") as remove,
            self.assertRaisesRegex(RuntimeError, "Python 3.11"),
        ):
            builder.prepare_generated_root(path, "build", False)
        remove.assert_not_called()
        self.assertEqual(artifact.read_bytes(), b"preserve")

    def test_no_clean_does_not_claim_nonempty_unowned_directory(self) -> None:
        path = self.workspace / "unowned-incremental"
        path.mkdir()
        sentinel = path / "source.txt"
        sentinel.write_text("preserve", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            builder.prepare_generated_root(path, "build", False, clean=False)
        self.assertEqual({entry.name for entry in path.iterdir()}, {sentinel.name})

    def test_dry_run_does_not_create_or_clean_directories(self) -> None:
        missing = self.workspace / "dry-missing"
        builder.prepare_generated_root(missing, "build", True)
        self.assertFalse(missing.exists())
        owned = self.workspace / "dry-owned"
        self.mark(owned)
        artifact = owned / "firmware.bin"
        artifact.write_bytes(b"keep")
        builder.prepare_generated_root(owned, "build", True)
        self.assertEqual(artifact.read_bytes(), b"keep")

    def test_both_roots_are_checked_before_any_cleanup_or_idf(self) -> None:
        build = self.workspace / "valid-build"
        self.mark(build)
        artifact = build / "object.o"
        artifact.write_bytes(b"preserve")
        output = self.workspace / "unowned-output"
        output.mkdir()
        (output / "source.txt").write_text("preserve", encoding="utf-8")
        profile = builder.BoardProfile("test-board", False, False)
        for options in ({}, {"dry_run": True}, {"no_clean": True}):
            with (
                self.subTest(options=options),
                mock.patch.object(builder, "parse_args", return_value=self.args(build, output, **options)),
                mock.patch.object(builder, "supported_boards", return_value=[profile]),
                mock.patch.object(builder, "idf_command") as idf,
                mock.patch.object(builder, "build_variant") as compile_variant,
                self.assertRaises(RuntimeError),
            ):
                builder.main()
            idf.assert_not_called()
            compile_variant.assert_not_called()
            self.assertEqual(artifact.read_bytes(), b"preserve")

    def test_main_rejects_workspace_parent_even_without_clean(self) -> None:
        profile = builder.BoardProfile("test-board", False, False)
        for options in ({}, {"dry_run": True}, {"no_clean": True}):
            with (
                self.subTest(options=options),
                mock.patch.object(builder, "parse_args", return_value=self.args(
                    self.workspace, self.workspace / "output", **options
                )),
                mock.patch.object(builder, "supported_boards", return_value=[profile]),
                mock.patch.object(builder, "idf_command") as idf,
                mock.patch.object(builder.shutil, "rmtree") as remove,
                self.assertRaises(RuntimeError),
            ):
                builder.main()
            idf.assert_not_called()
            remove.assert_not_called()

    @unittest.skipUnless(SAFE_CLEANUP, "cleanup requires Python 3.11+ and symlink-safe rmtree")
    def test_main_accepts_default_roots_and_records_ownership(self) -> None:
        build = self.project / "build"
        output = self.project / "releases"
        result, idf, compile_variant = self.run_main(self.args(build, output))
        self.assertEqual(result, 0)
        idf.assert_called_once()
        compile_variant.assert_called_once()
        builder.validate_generated_root(build, "build")
        builder.validate_generated_root(output, "output")
        self.assertTrue((build / MARKER).is_file())
        self.assertTrue((output / MARKER).is_file())

    def test_main_dry_run_leaves_custom_roots_absent(self) -> None:
        build = self.workspace / "custom-build"
        output = self.workspace / "custom-output"
        result, _, compile_variant = self.run_main(self.args(build, output, dry_run=True))
        self.assertEqual(result, 0)
        self.assertFalse(build.exists())
        self.assertFalse(output.exists())
        compile_variant.assert_called_once()


if __name__ == "__main__":
    unittest.main()
