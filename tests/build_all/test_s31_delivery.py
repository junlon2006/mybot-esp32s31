#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Delivery tests use real synthetic ELF headers, maps and merged flash bytes."""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("s31_delivery_under_test", ROOT / "scripts/check_s31_build.py")
assert SPEC and SPEC.loader
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class S31DeliveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="mybot-s31-delivery-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()
        self.merged = self.root / "esp32-s31-korvo-1-zh-CN.bin"
        self.description = {
            "target": "esp32s31", "git_revision": "v6.1", "app_elf": "mybot_esp32s31.elf",
            "app_bin": "mybot_esp32s31.bin", "project_name": "mybot_esp32s31", "project_version": "0.1.0",
        }
        self.write_description()
        self.config = (
            'CONFIG_IDF_TARGET="esp32s31"\nCONFIG_ESPTOOLPY_FLASHSIZE_16MB=y\n'
            'CONFIG_SPIRAM=y\nCONFIG_SPIRAM_MODE_OCT=y\nCONFIG_CODEC_ES8389_SUPPORT=y\n'
            'CONFIG_MYBOT_LVGL_UI=y\nCONFIG_MYBOT_AUDIO_PTIME_60=y\nCONFIG_MYBOT_AUDIO_PTIME_MS=60\n'
            'CONFIG_MYBOT_LANGUAGE_TAG="zh-CN"\n# CONFIG_MYBOT_ENABLE_VIDEO is not set\n'
        )
        (self.build / "sdkconfig").write_text(self.config)
        header = bytearray(52)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = (243).to_bytes(2, "little")
        (self.build / "mybot_esp32s31.elf").write_bytes(header)
        self.map_text = 'libagora-rtc-sdk.a(rtm.c.obj)\n' + ''.join(
            f'{symbol}  esp-idf/aosl/libaosl.a(port.c.obj)\n' for symbol in checker.AOSL_SYMBOLS
        )
        (self.build / "mybot_esp32s31.map").write_text(self.map_text)
        (self.build / "bootloader").mkdir()
        (self.build / "bootloader/bootloader.bin").write_bytes(b"bootloader-data")
        (self.build / "mybot_esp32s31.bin").write_bytes(b"application-data")
        self.flash = {
            "extra_esptool_args": {
                "chip": "esp32s31", "before": "default-reset", "after": "hard-reset", "stub": False,
            },
            "flash_settings": {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"},
            "flash_files": {"0x2000": "bootloader/bootloader.bin", "0x20000": "mybot_esp32s31.bin"},
        }
        self.write_flash()
        image = bytearray(b"\xff" * (0x20000 + len(b"application-data")))
        image[0x2000:0x2000 + len(b"bootloader-data")] = b"bootloader-data"
        image[0x20000:] = b"application-data"
        self.merged.write_bytes(image)

    def write_description(self) -> None:
        (self.build / "project_description.json").write_text(json.dumps(self.description))

    def write_flash(self) -> None:
        (self.build / "flasher_args.json").write_text(json.dumps(self.flash))

    def test_delivery_has_exact_offsets_hashes_versions_and_no_vendor_inputs(self) -> None:
        with mock.patch.object(checker, "source_revision", return_value={"revision": "a" * 40, "dirty": False}):
            metadata = checker.package(self.build, self.merged, "zh-CN")
        manifest = json.loads(self.merged.with_suffix(".manifest.json").read_text())
        self.assertEqual(metadata, manifest)
        self.assertEqual(manifest["image"]["flash_offset"], "0x0")
        self.assertEqual(manifest["image"]["sha256"], hashlib.sha256(self.merged.read_bytes()).hexdigest())
        self.assertEqual([s["offset"] for s in manifest["segments"]], ["0x2000", "0x20000"])
        self.assertEqual(manifest["project_version"], "0.1.0")
        self.assertEqual(manifest["idf_version"], "v6.1")
        self.assertFalse(manifest["video_enabled"])
        self.assertEqual(manifest["rtsa"]["archive_sha256"], checker.RTSA_SHA256)
        self.assertEqual(self.merged.with_suffix(".sha256").read_text(), f'{manifest["image"]["sha256"]}  {self.merged.name}\n')
        self.assertEqual(manifest["extra_esptool_args"], self.flash["extra_esptool_args"])
        self.assertEqual(manifest["flash_settings"], self.flash["flash_settings"])
        expected_command = [
            "python", "-m", "esptool", "--chip", "esp32s31", "--port", "PORT",
            "--before", "default-reset", "--after", "hard-reset", "--no-stub", "write-flash",
            "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
            "0x0", self.merged.name,
        ]
        self.assertEqual(manifest["flash_command"], expected_command)
        self.assertIn(shlex.join(expected_command), self.merged.with_suffix(".flash.txt").read_text())
        self.assertNotIn(str(self.root), self.merged.with_suffix(".manifest.json").read_text())
        self.assertEqual({p.name for p in self.root.iterdir()}, {
            "build", self.merged.name, self.merged.with_suffix(".manifest.json").name,
            self.merged.with_suffix(".sha256").name, self.merged.with_suffix(".flash.txt").name,
        })

    def test_video_and_wrong_language_are_rejected_before_packaging(self) -> None:
        for config, language in ((self.config + 'CONFIG_MYBOT_ENABLE_VIDEO=y\n', "zh-CN"), (self.config, "en-US")):
            with self.subTest(language=language):
                (self.build / "sdkconfig").write_text(config)
                with self.assertRaises(RuntimeError):
                    checker.package(self.build, self.merged, language)
                self.assertFalse(self.merged.with_suffix(".manifest.json").exists())

    def test_wrong_architecture_and_elf_class_are_rejected(self) -> None:
        path = self.build / "mybot_esp32s31.elf"
        original = path.read_bytes()
        for location, value in ((18, 62), (4, 2), (5, 2)):
            with self.subTest(location=location):
                data = bytearray(original)
                data[location] = value
                path.write_bytes(data)
                with self.assertRaisesRegex(RuntimeError, "ELF32.*RISC-V"):
                    checker.verify_build(self.build, "zh-CN")

    def test_wrong_target_and_idf_version_are_rejected(self) -> None:
        for key, value in (("target", "esp32s3"), ("git_revision", "v5.5.2")):
            with self.subTest(key=key):
                self.description[key] = value
                self.write_description()
                with self.assertRaises(RuntimeError):
                    checker.verify_build(self.build, "zh-CN")
                self.description[key] = "esp32s31" if key == "target" else "v6.1"

    def test_vendor_host_members_and_wrong_aosl_ownership_are_rejected(self) -> None:
        variants = (
            self.map_text + 'libagora-rtc-sdk.a(mm.c.o)\n',
            self.map_text.replace("esp-idf/aosl/libaosl.a", "lib/libagora-rtc-sdk.a"),
            self.map_text.replace('libagora-rtc-sdk.a(rtm.c.obj)\n', ''),
        )
        for text in variants:
            with self.subTest(text=text[:60]):
                (self.build / "mybot_esp32s31.map").write_text(text)
                with self.assertRaises(RuntimeError):
                    checker.verify_build(self.build, "zh-CN")

    def test_stale_wrong_offset_truncated_and_extended_images_are_rejected(self) -> None:
        original = self.merged.read_bytes()
        variants = [original[0x2000:], original[:-1], original + b"extra", original.replace(b"application-data", b"wrong-image-data")]
        for data in variants:
            with self.subTest(size=len(data)):
                self.merged.write_bytes(data)
                with self.assertRaisesRegex(RuntimeError, "merged image"):
                    checker.package(self.build, self.merged, "zh-CN")

    def test_escaping_and_overlapping_flash_files_are_rejected(self) -> None:
        outside = self.root / "external.bin"
        outside.write_bytes(b"external")
        self.flash["flash_files"]["0x20000"] = "../external.bin"
        self.write_flash()
        with self.assertRaisesRegex(RuntimeError, "invalid build file"):
            checker.verify_build(self.build, "zh-CN")
        self.flash["flash_files"] = {"0x2000": "bootloader/bootloader.bin", "0x2001": "mybot_esp32s31.bin"}
        self.write_flash()
        with self.assertRaisesRegex(RuntimeError, "overlapping"):
            checker.verify_build(self.build, "zh-CN")

    def test_symlink_output_is_rejected_without_changing_target(self) -> None:
        target = self.root / "preserve.json"
        target.write_text("preserve")
        self.merged.with_suffix(".manifest.json").symlink_to(target)
        with self.assertRaisesRegex(RuntimeError, "symlink output"):
            checker.package(self.build, self.merged, "zh-CN")
        self.assertEqual(target.read_text(), "preserve")

    def test_bundled_vendor_files_are_pinned_and_changed_payload_fails(self) -> None:
        checker.verify_bundled_rtsa(ROOT)
        vendor = self.root / "components/mybot_stack/agora_rtc"
        for name in checker.RTSA_FILES:
            destination = vendor / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(b"wrong-architecture-or-package")
        with self.assertRaisesRegex(RuntimeError, "fingerprint mismatch"):
            checker.verify_bundled_rtsa(self.root)

    def test_flash_command_quotes_image_names_and_preserves_stub_selection(self) -> None:
        image = self.root / "S31 firmware.bin"
        self.merged.rename(image)
        self.flash["extra_esptool_args"]["stub"] = True
        self.write_flash()
        metadata = checker.package(self.build, image, "zh-CN")
        self.assertNotIn("--no-stub", metadata["flash_command"])
        self.assertEqual(metadata["flash_command"][-2:], ["0x0", image.name])
        command_line = next(
            line for line in image.with_suffix(".flash.txt").read_text().splitlines()
            if line.startswith("python -m esptool")
        )
        self.assertEqual(shlex.split(command_line), metadata["flash_command"])

    def test_missing_reset_modes_or_ambiguous_stub_flag_are_rejected(self) -> None:
        for key, value in (("stub", "false"), ("before", None), ("after", "")):
            with self.subTest(key=key):
                self.flash["extra_esptool_args"][key] = value
                self.write_flash()
                with self.assertRaisesRegex(RuntimeError, "reset modes and stub"):
                    checker.verify_build(self.build, "zh-CN")
                self.flash["extra_esptool_args"] = {
                    "chip": "esp32s31", "before": "default-reset", "after": "hard-reset", "stub": False,
                }


class WorkflowDeliveryTests(unittest.TestCase):
    def test_ci_merge_output_is_absolute_and_uploads_only_delivery_files(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn('merged="$GITHUB_WORKSPACE/releases/', workflow)
        self.assertIn('merge-bin -o "$merged"', workflow)
        self.assertNotIn('merge-bin -o "$BUILD_DIR/', workflow)
        self.assertIn('scripts/check_s31_build.py --build-dir "$BUILD_DIR"', workflow)
        upload = workflow.split("      - name: Upload firmware", 1)[1]
        for suffix in (".bin", ".manifest.json", ".sha256", ".flash.txt"):
            self.assertIn('${{ matrix.language }}' + suffix, upload)
        self.assertNotIn(".map", upload)
        self.assertNotIn(".tgz", upload)
        self.assertNotIn("vendor", upload)
        self.assertIn("scripts/check_s31_build.py --vendor-only", workflow)
        self.assertNotIn("rtsa_archive_url", workflow)
        self.assertNotIn("RTSA_S31_DOWNLOAD_TOKEN", workflow)
        self.assertNotIn("if: github.event_name == 'workflow_dispatch'", workflow)


if __name__ == "__main__":
    unittest.main()
