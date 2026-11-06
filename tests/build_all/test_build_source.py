#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build-time provenance uses real Git inputs and frozen synthetic artifacts."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("capture_source_under_test", ROOT / "scripts/capture_build_source.py")
assert SPEC and SPEC.loader
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class BuildSourceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="mybot-source-")
        self.addCleanup(self.temporary.cleanup)
        self.project = Path(self.temporary.name).resolve()
        self.build = self.project / "build"
        self.build.mkdir()
        (self.project / "main").mkdir()
        (self.project / "main/app.c").write_text("const int value = 1;\n")
        (self.project / "CMakeLists.txt").write_text("# project build input\n")
        (self.project / "dependencies.lock").write_text("pinned-dependency-version\n")
        (self.project / ".gitignore").write_text("build/\nmanaged_components/\n.env\n")
        (self.build / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32s31"\n')
        self.git("init", "--quiet")
        self.commit("A")
        self.revision_a = self.git("rev-parse", "HEAD").strip()
        (self.build / "project_description.json").write_text(json.dumps({
            "app_elf": "app.elf", "app_bin": "app.bin",
        }))
        (self.build / "flasher_args.json").write_text(json.dumps({
            "flash_files": {"0x0": "bootloader.bin", "0x20000": "app.bin"},
        }))
        (self.build / "bootloader.bin").write_bytes(b"bootloader-A")

    def git(self, *args: str) -> str:
        return subprocess.run(["git", *args], cwd=self.project, check=True,
                              capture_output=True, text=True).stdout

    def commit(self, message: str) -> None:
        self.git("add", "-A")
        self.git("-c", "user.name=Host Test", "-c", "user.email=host-test@example.invalid",
                 "commit", "--quiet", "-m", message)

    def complete_build(self) -> dict:
        capture.capture(self.project, self.build)
        value = json.loads((self.build / capture.SNAPSHOT).read_text())
        payload = capture.marker(value)
        (self.build / "app.elf").write_bytes(b"ELF-" + payload)
        (self.build / "app.bin").write_bytes(b"BIN-" + payload)
        return capture.finalize(self.project, self.build)

    def test_ci_workspace_trust_recovers_container_ownership_without_trusting_other_repos(self) -> None:
        env = dict(os.environ, GIT_CONFIG_GLOBAL=str(self.build / "global.gitconfig"),
                   GIT_CONFIG_NOSYSTEM="1", GIT_TEST_ASSUME_DIFFERENT_OWNER="1", LC_ALL="C",
                   GITHUB_WORKSPACE=str(self.project))
        command = [sys.executable, str(ROOT / "scripts/capture_build_source.py"), "capture",
                   "--project-root", str(self.project), "--build-dir", str(self.build)]
        unsafe = subprocess.run(["git", "rev-parse", "--verify", "HEAD"], cwd=self.project,
                                env=env, capture_output=True, text=True)
        if unsafe.returncode == 0:
            self.skipTest("installed Git does not support forced ownership checks")
        self.assertEqual(unsafe.returncode, 128)
        self.assertIn("dubious ownership", unsafe.stderr)
        failed = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("dubious ownership", failed.stderr)
        self.assertFalse((self.build / capture.SNAPSHOT).exists())
        self.assertFalse((self.build / capture.GENERATED_C).exists())

        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        step = workflow.split("      - name: Allow Git access to checked-out workspace\n", 1)[1]
        body = step.split("        run: |\n", 1)[1].split("      - ", 1)[0]
        body = "\n".join(line.removeprefix("          ") for line in body.splitlines())
        subprocess.run(["bash", "-e", "-c", body], cwd=self.project, env=env,
                       check=True, capture_output=True, text=True)
        configured = subprocess.run(["git", "config", "--global", "--get-all", "safe.directory"],
                                    env=env, check=True, capture_output=True, text=True)
        self.assertEqual(configured.stdout.splitlines(), [str(self.project)])

        for dirty in (False, True):
            with self.subTest(dirty=dirty):
                if dirty:
                    (self.project / "main/app.c").write_text("changed source\n")
                subprocess.run(command, env=env, check=True, capture_output=True, text=True)
                value = json.loads((self.build / capture.SNAPSHOT).read_text())
                for name in ("app.elf", "app.bin"):
                    (self.build / name).write_bytes(capture.marker(value))
                finalize = command.copy()
                finalize[2] = "finalize"
                subprocess.run(finalize, env=env, check=True, capture_output=True, text=True)
                built = capture.verify(self.build)
                self.assertEqual(built["snapshot"]["source"]["revision"], self.revision_a)
                self.assertEqual(built["snapshot"]["source"]["dirty"], dirty)

        unrelated = self.build / "unrelated"
        unrelated.mkdir()
        initialize_env = env.copy()
        initialize_env.pop("GIT_TEST_ASSUME_DIFFERENT_OWNER")
        subprocess.run(["git", "init", "--quiet", str(unrelated)], env=initialize_env, check=True)
        refused = subprocess.run(["git", "-C", str(unrelated), "status", "--porcelain"],
                                 env=env, capture_output=True, text=True)
        self.assertEqual(refused.returncode, 128)
        self.assertIn("dubious ownership", refused.stderr)

    def test_missing_repository_keeps_git_diagnostic_and_does_not_create_snapshot(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mybot-no-git-") as temporary:
            root = Path(temporary).resolve()
            build = root / "build"
            build.mkdir()
            (build / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32s31"\n')
            env = dict(os.environ, GIT_CONFIG_GLOBAL=str(build / "global.gitconfig"),
                       GIT_CONFIG_NOSYSTEM="1", LC_ALL="C")
            failed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/capture_build_source.py"), "capture",
                 "--project-root", str(root), "--build-dir", str(build)],
                env=env, capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("not a git repository", failed.stderr)
            self.assertIn("build provenance:", failed.stderr)
            self.assertFalse((build / capture.SNAPSHOT).exists())
            self.assertFalse((build / capture.GENERATED_C).exists())

    def test_existing_a_keeps_a_after_checkout_b_and_failed_b_capture(self) -> None:
        built = self.complete_build()
        (self.project / "main/app.c").write_text("const int value = 2;\n")
        self.commit("B")
        self.assertNotEqual(self.git("rev-parse", "HEAD").strip(), self.revision_a)
        self.assertEqual(capture.verify(self.build), built)
        capture.capture(self.project, self.build)  # A later build fails before linking.
        self.assertEqual(capture.verify(self.build), built)
        self.assertEqual(built["snapshot"]["source"]["revision"], self.revision_a)
        with self.assertRaisesRegex(RuntimeError, "not embedded"):
            capture.finalize(self.project, self.build)

    def test_unchanged_build_keeps_generated_source_and_stamp_timestamps(self) -> None:
        self.complete_build()
        paths = [self.build / name for name in (capture.SNAPSHOT, capture.GENERATED_C, capture.STAMP)]
        before = [path.stat().st_mtime_ns for path in paths]
        capture.capture(self.project, self.build)
        capture.finalize(self.project, self.build)
        self.assertEqual([path.stat().st_mtime_ns for path in paths], before)

    def test_incremental_edit_without_reconfigure_changes_embedded_identity(self) -> None:
        built = self.complete_build()
        old_c = (self.build / capture.GENERATED_C).read_bytes()
        (self.project / "main/app.c").write_text("const int value = 3;\n")
        capture.capture(self.project, self.build)
        value = json.loads((self.build / capture.SNAPSHOT).read_text())
        self.assertTrue(value["source"]["dirty"])
        self.assertNotEqual(value["source"]["inputs_sha256"], built["snapshot"]["source"]["inputs_sha256"])
        self.assertNotEqual((self.build / capture.GENERATED_C).read_bytes(), old_c)
        with self.assertRaisesRegex(RuntimeError, "not embedded"):
            capture.finalize(self.project, self.build)
        rebuilt = self.complete_build()
        self.assertEqual(capture.verify(self.build), rebuilt)
        self.assertTrue(rebuilt["snapshot"]["source"]["dirty"])

    def test_new_commit_even_with_same_source_requires_relink(self) -> None:
        self.complete_build()
        original = (self.build / capture.GENERATED_C).read_bytes()
        (self.project / "README.md").write_text("Unrelated documentation change.\n")
        self.commit("B with unchanged application")
        capture.capture(self.project, self.build)
        self.assertNotEqual((self.build / capture.GENERATED_C).read_bytes(), original)
        with self.assertRaisesRegex(RuntimeError, "not embedded"):
            capture.finalize(self.project, self.build)

    def test_source_config_or_checkout_changes_during_build_reject_stamp(self) -> None:
        built = self.complete_build()
        for kind in ("source", "config", "commit"):
            with self.subTest(kind=kind):
                self.git("reset", "--hard", self.revision_a)
                (self.build / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32s31"\n')
                capture.capture(self.project, self.build)
                if kind == "source":
                    (self.project / "main/app.c").write_text("changed during compilation\n")
                elif kind == "config":
                    (self.build / "sdkconfig").write_text("changed effective config\n")
                else:
                    self.git("-c", "user.name=Host Test", "-c", "user.email=host-test@example.invalid",
                             "commit", "--allow-empty", "--quiet", "-m", "B")
                with self.assertRaisesRegex(RuntimeError, "changed during the build"):
                    capture.finalize(self.project, self.build)
                self.assertEqual(json.loads((self.build / capture.STAMP).read_text()), built)

    def test_new_project_sources_are_dirty_but_private_and_generated_files_excluded(self) -> None:
        clean = capture.snapshot(self.project, self.build)
        (self.project / ".env").write_text("SECRET_VALUE=do-not-include\n")
        (self.project / "DEVICE_API.md").write_text("temporary service notes\n")
        (self.project / "device-translation-flow.zh-CN.html").write_text("temporary simulator\n")
        (self.project / "managed_components").mkdir()
        (self.project / "managed_components/generated.c").write_text("ignored cache\n")
        self.assertEqual(capture.snapshot(self.project, self.build), clean)
        (self.project / "main/new.h").write_text("#define NEW_SOURCE 1\n")
        dirty = capture.snapshot(self.project, self.build)
        self.assertTrue(dirty["source"]["dirty"])
        self.assertNotEqual(dirty["source"]["inputs_sha256"], clean["source"]["inputs_sha256"])
        self.assertNotIn("SECRET_VALUE", json.dumps(dirty))

    def test_changed_tracked_inputs_cannot_be_reported_clean(self) -> None:
        for name in ("main/app.c", "dependencies.lock", "CMakeLists.txt"):
            with self.subTest(name=name):
                self.git("reset", "--hard", self.revision_a)
                (self.project / name).write_text("changed input\n")
                self.assertTrue(capture.snapshot(self.project, self.build)["source"]["dirty"])

    def test_embedded_wifi_html_is_a_build_input_but_root_simulator_is_not(self) -> None:
        page = self.project / "components/esp-wifi-connect/assets/wifi_configuration.html"
        page.parent.mkdir(parents=True)
        page.write_text("<html>Wi-Fi configuration</html>\n")
        self.commit("Embedded page")
        before = capture.snapshot(self.project, self.build)
        page.write_text("<html>Updated Wi-Fi configuration</html>\n")
        after = capture.snapshot(self.project, self.build)
        self.assertTrue(after["source"]["dirty"])
        self.assertNotEqual(after["source"]["inputs_sha256"], before["source"]["inputs_sha256"])
        (self.project / "device-translation-flow.zh-CN.html").write_text("temporary simulator\n")
        self.assertEqual(capture.snapshot(self.project, self.build), after)

    def test_index_flags_cannot_hide_a_dirty_source_build(self) -> None:
        clean = capture.snapshot(self.project, self.build)
        for flag in ("assume-unchanged", "skip-worktree"):
            with self.subTest(flag=flag):
                self.git("update-index", "--" + flag, "main/app.c")
                (self.project / "main/app.c").write_text("hidden source change\n")
                self.assertEqual(self.git("status", "--porcelain", "--untracked-files=no"), "")
                value = capture.snapshot(self.project, self.build)
                self.assertTrue(value["source"]["dirty"])
                self.assertNotEqual(value["source"]["inputs_sha256"], clean["source"]["inputs_sha256"])
                self.git("update-index", "--no-" + flag, "main/app.c")
                self.git("reset", "--hard", self.revision_a)

    def test_missing_or_replaced_elf_bin_config_and_flash_artifacts_are_rejected(self) -> None:
        self.complete_build()
        for name in ("app.elf", "app.bin", "sdkconfig", "bootloader.bin", "project_description.json", "flasher_args.json"):
            with self.subTest(name=name):
                path = self.build / name
                original = path.read_bytes()
                if name.endswith(".json"):
                    path.write_bytes(original + b"\n")
                else:
                    path.write_bytes(original + b"changed")
                with self.assertRaisesRegex(RuntimeError, "artifact mismatch"):
                    capture.verify(self.build)
                path.write_bytes(original)
        (self.build / capture.STAMP).unlink()
        with self.assertRaisesRegex(RuntimeError, "invalid build provenance artifact"):
            capture.verify(self.build)

    def test_copied_stamp_or_removed_embedded_marker_cannot_backfill_legacy_image(self) -> None:
        built = self.complete_build()
        (self.build / "app.elf").write_bytes(b"legacy ELF")
        (self.build / "app.bin").write_bytes(b"legacy BIN")
        for artifact in built["artifacts"]:
            artifact["sha256"] = capture.sha256(self.build / artifact["file"])
        capture.write_json(self.build / capture.STAMP, built)
        with self.assertRaisesRegex(RuntimeError, "not embedded"):
            capture.verify(self.build)

    def test_cmake_hooks_cover_components_elf_app_and_merge_bin(self) -> None:
        hook = (ROOT / "scripts/build_source.cmake").read_text()
        self.assertIn("BUILD_COMPONENTS", hook)
        self.assertIn("idf_component_get_property(mybot_source_target ${mybot_source_component} COMPONENT_LIB)", hook)
        self.assertIn("add_dependencies(${mybot_source_target} mybot_capture_build_source)", hook)
        self.assertIn("--undefined=mybot_build_source_sha256", hook)
        self.assertIn("DEPENDS gen_project_binary bootloader partition_table_bin", hook)
        self.assertIn("add_dependencies(app mybot_finalize_build_source)", hook)
        self.assertIn("add_dependencies(merge-bin mybot_finalize_build_source)", hook)


if __name__ == "__main__":
    unittest.main()
