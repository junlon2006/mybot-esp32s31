#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local builder paths, preserved outputs and IDF-managed configuration switching."""
from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/build_all.py"
SPEC = importlib.util.spec_from_file_location("local_builder_under_test", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class LocalBuilderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="mybot-local-builder-")
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name).resolve()
        self.project = self.workspace / "project"
        self.project.mkdir()
        self.build = self.project / "build/mosaico-v1.2"
        self.output = self.project / "releases"
        patch = mock.patch.object(builder, "PROJECT_ROOT", self.project)
        patch.start()
        self.addCleanup(patch.stop)
        patch = mock.patch.object(builder, "RELEASES_DIR", self.output)
        patch.start()
        self.addCleanup(patch.stop)
        self.printed = io.StringIO()
        redirect = contextlib.redirect_stdout(self.printed)
        redirect.__enter__()
        self.addCleanup(redirect.__exit__, None, None, None)

    def args(self, **updates: object) -> argparse.Namespace:
        values = dict(language="both", aec_mode="local", build_root=None,
                      output_root=self.output, dry_run=False, no_clean=False)
        values.update(updates)
        return argparse.Namespace(**values)

    def test_default_cli_reuses_local_build_and_explicit_root_selects_isolation(self) -> None:
        with mock.patch.object(sys, "argv", [str(SCRIPT)]):
            self.assertIsNone(builder.parse_args().build_root)
        with mock.patch.object(sys, "argv", [str(SCRIPT), "--build-root", "/tmp/ci-build"]):
            self.assertEqual(builder.parse_args().build_root, Path("/tmp/ci-build"))

    def test_default_local_build_preserves_unowned_roots_and_siblings(self) -> None:
        self.build.mkdir(parents=True)
        self.output.mkdir()
        config = self.build / "sdkconfig"
        sibling = self.build.parent / "diagnostic-baseline.bin"
        artifact = self.output / "previous-firmware.bin"
        for path in (config, sibling, artifact):
            path.write_bytes(b"preserve")
        build_inode = self.build.stat().st_ino
        for no_clean in (False, True):
            with (
                self.subTest(no_clean=no_clean),
                mock.patch.object(builder, "parse_args", return_value=self.args(no_clean=no_clean)),
                mock.patch.object(builder, "idf_command", return_value="idf.py"),
                mock.patch.object(builder, "prepare_generated_root") as clean,
                mock.patch.object(builder, "build_variant", return_value=self.output / "image.bin") as build,
            ):
                self.assertEqual(builder.main(), 0)
            clean.assert_not_called()
            self.assertEqual(build.call_count, 2)
            for call in build.call_args_list:
                self.assertEqual(call.args[2], self.build)
                self.assertTrue(call.kwargs["local"])
            for path in (config, sibling, artifact):
                self.assertEqual(path.read_bytes(), b"preserve")
            self.assertEqual(self.build.stat().st_ino, build_inode)
            self.assertFalse((self.build / builder.GENERATED_ROOT_MARKER).exists())
            self.assertFalse((self.output / builder.GENERATED_ROOT_MARKER).exists())

    def test_default_dry_run_plans_both_languages_in_one_directory_without_mutation(self) -> None:
        with (
            mock.patch.object(builder, "parse_args", return_value=self.args(dry_run=True)),
            mock.patch.object(builder, "idf_command", return_value="idf.py"),
            mock.patch.object(builder.subprocess, "run") as execute,
        ):
            self.assertEqual(builder.main(), 0)
        execute.assert_not_called()
        self.assertFalse(self.build.parent.exists())
        self.assertFalse(self.output.exists())
        commands = [line for line in self.printed.getvalue().splitlines()
                    if line.startswith(">>> idf.py")]
        self.assertEqual(len(commands), 10)
        self.assertTrue(all(f"-B {self.build} " in line for line in commands))
        self.assertEqual(sum(f"-DSDKCONFIG={self.build / 'sdkconfig'} " in line
                             for line in commands), 2)
        self.assertTrue(all("-D" not in line for line in commands if "reconfigure" not in line))
        self.assertEqual(sum("confserver" in line for line in commands), 2)
        self.assertNotIn("Cleaning", self.printed.getvalue())
        self.assertNotIn("build-esp-mosaico", self.printed.getvalue())

    def test_overlapping_local_roots_fail_before_idf_or_creation(self) -> None:
        for output in (self.build, self.build / "out", self.build.parent):
            with (
                self.subTest(output=output),
                mock.patch.object(builder, "parse_args", return_value=self.args(output_root=output)),
                mock.patch.object(builder, "idf_command") as idf,
                self.assertRaises(RuntimeError),
            ):
                builder.main()
            idf.assert_not_called()
            self.assertFalse(self.build.parent.exists())

    def test_symlinked_local_build_ancestor_fails_without_touching_target(self) -> None:
        external = self.workspace / "external"
        external.mkdir()
        sentinel = external / "keep.txt"
        sentinel.write_bytes(b"preserve")
        (self.project / "build").symlink_to(external, target_is_directory=True)
        with (
            mock.patch.object(builder, "parse_args", return_value=self.args()),
            mock.patch.object(builder, "idf_command") as idf,
            self.assertRaises(RuntimeError),
        ):
            builder.main()
        idf.assert_not_called()
        self.assertEqual(sentinel.read_bytes(), b"preserve")
        self.assertFalse((external / "mosaico-v1.2").exists())

    def test_invalid_output_root_fails_before_creating_local_build(self) -> None:
        self.output.write_bytes(b"not a directory")
        with (
            mock.patch.object(builder, "parse_args", return_value=self.args()),
            mock.patch.object(builder, "idf_command") as idf,
            self.assertRaises(OSError),
        ):
            builder.main()
        idf.assert_not_called()
        self.assertFalse(self.build.parent.exists())
        self.assertEqual(self.output.read_bytes(), b"not a directory")

    def test_configuration_aliases_are_rejected_before_idf(self) -> None:
        self.build.mkdir(parents=True)
        sentinel = self.workspace / "keep.config"
        sentinel.write_bytes(b"preserve")
        for name in ("sdkconfig", "sdkconfig.old"):
            path = self.build / name
            for kind in ("symlink", "hardlink", "directory"):
                if kind == "symlink":
                    path.symlink_to(sentinel)
                elif kind == "hardlink":
                    path.hardlink_to(sentinel)
                else:
                    path.mkdir()
                with (
                    self.subTest(name=name, kind=kind),
                    mock.patch.object(builder, "parse_args", return_value=self.args()),
                    mock.patch.object(builder, "idf_command") as idf,
                    self.assertRaises(RuntimeError),
                ):
                    builder.main()
                idf.assert_not_called()
                self.assertEqual(sentinel.read_bytes(), b"preserve")
                path.rmdir() if kind == "directory" else path.unlink()

    def test_local_build_configures_before_compiling_and_packages_before_next_variant(self) -> None:
        events = []
        def idf_run(idf: str, args: list[str], dry_run: bool) -> None:
            self.assertEqual(args[:2], ["-B", str(self.build)])
            if args[-1] == "reconfigure":
                self.assertIn("-DMYBOT_MOSAICO_UAC_TEST=OFF", args)
            else:
                self.assertTrue(all(not arg.startswith("-D") for arg in args))
            events.append("merge-bin" if "merge-bin" in args else args[-1])
        with (
            mock.patch.object(builder, "run", side_effect=idf_run),
            mock.patch.object(builder, "configure_local_variant",
                              side_effect=lambda *args: events.append("configure")) as configure,
            mock.patch.object(builder.subprocess, "run",
                              side_effect=lambda *args, **kwargs: events.append("package")),
        ):
            image = builder.build_variant("idf.py", "zh-CN", self.build, self.output,
                                          False, "local", local=True)
        self.assertEqual(events, ["reconfigure", "configure", "build", "size", "merge-bin", "package"])
        self.assertEqual(configure.call_args.args[2], self.build / "sdkconfig")
        self.assertEqual(image, self.output / "esp-mosaico-v1.2-zh-CN.bin")

    def test_output_symlinks_hardlinks_and_nonregular_entries_fail_before_idf(self) -> None:
        self.output.mkdir()
        sentinel = self.workspace / "keep.txt"
        sentinel.write_bytes(b"preserve")
        output = self.output / "esp-mosaico-v1.2-zh-CN.bin"
        for kind in ("symlink", "hardlink", "directory"):
            if kind == "symlink":
                output.symlink_to(sentinel)
            elif kind == "hardlink":
                output.hardlink_to(sentinel)
            else:
                output.mkdir()
            with (
                self.subTest(kind=kind),
                mock.patch.object(builder, "run") as execute,
                self.assertRaises(RuntimeError),
            ):
                builder.build_variant("idf.py", "zh-CN", self.build, self.output,
                                      False, "local", local=True)
            execute.assert_not_called()
            self.assertEqual(sentinel.read_bytes(), b"preserve")
            output.rmdir() if kind == "directory" else output.unlink()

    def test_metadata_output_alias_is_rejected_before_merge(self) -> None:
        self.output.mkdir()
        sentinel = self.workspace / "keep.json"
        sentinel.write_bytes(b"preserve")
        (self.output / "esp-mosaico-v1.2-zh-CN.manifest.json").symlink_to(sentinel)
        with mock.patch.object(builder, "run") as execute, self.assertRaises(RuntimeError):
            builder.build_variant("idf.py", "zh-CN", self.build, self.output, False, local=True)
        execute.assert_not_called()
        self.assertEqual(sentinel.read_bytes(), b"preserve")


class LocalConfigurationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="mybot-config-server-")
        self.addCleanup(self.temporary.cleanup)
        self.sdkconfig = Path(self.temporary.name) / "sdkconfig"
        self.base = ["-B", str(self.sdkconfig.parent), f"-DSDKCONFIG={self.sdkconfig}"]
        self.printed = io.StringIO()
        redirect = contextlib.redirect_stdout(self.printed)
        redirect.__enter__()
        self.addCleanup(redirect.__exit__, None, None, None)

    def saved_config(self, language: str, mode: str, *, ptime: int = 60) -> str:
        symbols = {"local": "LOCAL_AEC", "cloud": "CLOUD_AEC", "disabled": "AEC_DISABLED"}
        return (f'CONFIG_MYBOT_LANGUAGE_TAG="{language}"\n'
                f'CONFIG_MYBOT_{symbols[mode]}=y\nCONFIG_MYBOT_AUDIO_PTIME_MS={ptime}\n'
                'CONFIG_USER_SETTING="preserve"\n')

    def test_each_language_and_aec_choice_uses_idf_protocol_without_direct_config_edits(self) -> None:
        for language in ("zh-CN", "en-US"):
            for mode in ("local", "cloud", "disabled"):
                before = self.saved_config(language, mode)
                self.sdkconfig.write_text(before)
                output = (json.dumps({"version": 3, "values": {"private": "do-not-print"}}) + "\n"
                          + json.dumps({"version": 2, "values": {}}) + "\n")
                with (
                    self.subTest(language=language, mode=mode),
                    mock.patch.object(builder.subprocess, "run",
                                      return_value=subprocess.CompletedProcess([], 0, output, "")) as execute,
                ):
                    builder.configure_local_variant("idf.py", self.base, self.sdkconfig,
                                                    language, mode, False)
                call = execute.call_args
                self.assertEqual(call.args[0][:2], ["idf.py", "--preview"])
                self.assertIn("confserver", call.args[0])
                request = json.loads(call.kwargs["input"])
                self.assertEqual(request["version"], 2)
                self.assertIn("save", request)
                self.assertIsNone(request["save"])
                language_key = "MYBOT_LANGUAGE_ZH_CN" if language == "zh-CN" else "MYBOT_LANGUAGE_EN_US"
                self.assertTrue(request["set"][language_key])
                aec_key = {"local": "MYBOT_LOCAL_AEC", "cloud": "MYBOT_CLOUD_AEC",
                           "disabled": "MYBOT_AEC_DISABLED"}[mode]
                self.assertTrue(request["set"][aec_key])
                self.assertTrue(request["set"]["MYBOT_AUDIO_PTIME_60"])
                self.assertEqual(self.sdkconfig.read_text(), before)
        self.assertNotIn("do-not-print", self.printed.getvalue())

    def test_configuration_dry_run_never_starts_server_or_creates_config(self) -> None:
        with mock.patch.object(builder.subprocess, "run") as execute:
            builder.configure_local_variant("idf.py", self.base, self.sdkconfig, "en-US", "cloud", True)
        execute.assert_not_called()
        self.assertFalse(self.sdkconfig.exists())

    def test_server_failure_reports_stderr_without_printing_full_configuration(self) -> None:
        failure = subprocess.CalledProcessError(
            1, ["idf.py"], output="private-full-configuration", stderr="configuration rejected",
        )
        with (
            mock.patch.object(builder.subprocess, "run", side_effect=failure),
            self.assertRaisesRegex(RuntimeError, "configuration rejected") as raised,
        ):
            builder.configure_local_variant("idf.py", self.base, self.sdkconfig, "zh-CN", "local", False)
        self.assertNotIn("private-full-configuration", str(raised.exception))
        self.assertNotIn("private-full-configuration", self.printed.getvalue())

    def test_server_errors_and_missing_acknowledgement_are_rejected(self) -> None:
        self.sdkconfig.write_text(self.saved_config("zh-CN", "local"))
        outputs = ["not JSON", json.dumps({"version": 3, "values": {}}),
                   json.dumps({"version": 2, "values": {}, "error": ["setting rejected"]})]
        for output in outputs:
            with (
                self.subTest(output=output),
                mock.patch.object(builder.subprocess, "run",
                                  return_value=subprocess.CompletedProcess([], 0, output, "")),
                self.assertRaises(RuntimeError),
            ):
                builder.configure_local_variant("idf.py", self.base, self.sdkconfig, "zh-CN", "local", False)

    def test_stale_or_inconsistent_saved_choices_are_rejected(self) -> None:
        configs = [self.saved_config("en-US", "local"), self.saved_config("zh-CN", "cloud"),
                   self.saved_config("zh-CN", "local", ptime=20),
                   self.saved_config("zh-CN", "local") + "CONFIG_MYBOT_CLOUD_AEC=y\n"]
        response = json.dumps({"version": 2, "values": {}})
        for config in configs:
            self.sdkconfig.write_text(config)
            with (
                self.subTest(config=config),
                mock.patch.object(builder.subprocess, "run",
                                  return_value=subprocess.CompletedProcess([], 0, response, "")),
                self.assertRaises(RuntimeError),
            ):
                builder.configure_local_variant("idf.py", self.base, self.sdkconfig, "zh-CN", "local", False)


if __name__ == "__main__":
    unittest.main()
