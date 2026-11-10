#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Delivery tests use real synthetic ELF headers, maps and merged flash bytes."""
from __future__ import annotations

import hashlib
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import re
import shlex
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import capture_build_source as provenance

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
        (self.build / "CMakeCache.txt").write_text("MYBOT_BOARD:INTERNAL=esp-mosaico-v1.2\n")
        self.merged = self.root / "esp-mosaico-v1.2-zh-CN.bin"
        self.description = {
            "target": "esp32s31", "git_revision": "v6.1", "app_elf": "mybot_esp32s31.elf",
            "app_bin": "mybot_esp32s31.bin", "project_name": "mybot_esp32s31", "project_version": "0.1.0",
        }
        self.write_description()
        self.config = (
            'CONFIG_IDF_TARGET="esp32s31"\nCONFIG_ESPTOOLPY_FLASHSIZE_16MB=y\n'
            'CONFIG_SPIRAM=y\nCONFIG_SPIRAM_MODE_OCT=y\nCONFIG_CODEC_ES8311_SUPPORT=y\n'
            '# CONFIG_CODEC_ES8389_SUPPORT is not set\n'
            'CONFIG_MYBOT_LVGL_UI=y\nCONFIG_MYBOT_AUDIO_PTIME_60=y\nCONFIG_MYBOT_AUDIO_PTIME_MS=60\n'
            'CONFIG_MYBOT_LANGUAGE_TAG="zh-CN"\n# CONFIG_MYBOT_ENABLE_VIDEO is not set\n'
            'CONFIG_MYBOT_LOCAL_AEC=y\n# CONFIG_MYBOT_CLOUD_AEC is not set\n'
            '# CONFIG_MYBOT_AEC_DISABLED is not set\n'
        )
        (self.build / "sdkconfig").write_text(self.config)
        header = bytearray(52)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = (243).to_bytes(2, "little")
        (self.build / "mybot_esp32s31.elf").write_bytes(header)
        self.map_text = 'libagora-rtc-sdk.a(rtm.c.obj)\n' + ''.join(
            f'{symbol}  esp-idf/aosl/libaosl.a(port.c.obj)\n' for symbol in checker.AOSL_SYMBOLS
        )
        self.local_aec_live = "".join(
            f" .text.{symbol}\n                0x{0x40010000 + index * 0x100:08x}       0x40 {owner}\n"
            f"                0x{0x40010000 + index * 0x100:08x}                {symbol}\n"
            for index, (symbol, owner) in enumerate((
                ("mosaico_local_aec_create", "esp-idf/mybot_platform/libmybot_platform.a(mosaico_local_aec.c.obj)"),
                ("mosaico_local_aec_feed", "esp-idf/mybot_platform/libmybot_platform.a(mosaico_local_aec.c.obj)"),
                ("aec_create_from_config", "/project/managed_components/espressif__esp-sr/lib/esp32s31/libesp_audio_processor.a(esp_aec.c.obj)"),
                ("aec_process", "/project/managed_components/espressif__esp-sr/lib/esp32s31/libesp_audio_processor.a(esp_aec.c.obj)"),
            ))
        )
        self.map_text += "Linker script and memory map\n" + self.local_aec_live
        (self.build / "mybot_esp32s31.map").write_text(self.map_text)
        self.write_aec_commands("local")
        (self.build / "bootloader").mkdir()
        (self.build / "bootloader/bootloader.bin").write_bytes(b"bootloader-data")
        (self.build / "partition_table").mkdir()
        self.partitions = [
            ("nvs", 1, 2, 0x9000, 0x4000, 0),
            ("otadata", 1, 0, 0xD000, 0x2000, 0),
            ("phy_init", 1, 1, 0xF000, 0x1000, 0),
            ("ota_0", 0, 0x10, 0x20000, 0x3F0000, 0),
            ("ota_1", 0, 0x11, 0x410000, 0x3F0000, 0),
            ("assets", 1, 0x82, 0x800000, 0x800000, 0),
        ]
        self.write_partition_table()
        (self.build / "mybot_esp32s31.bin").write_bytes(b"application-data")
        self.flash = {
            "extra_esptool_args": {
                "chip": "esp32s31", "before": "default-reset", "after": "hard-reset", "stub": False,
            },
            "flash_settings": {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"},
            "flash_files": {"0x2000": "bootloader/bootloader.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x20000": "mybot_esp32s31.bin"},
            "partition-table": {"offset": "0x8000", "file": "partition_table/partition-table.bin"},
            "app": {"offset": "0x20000", "file": "mybot_esp32s31.bin"},
        }
        self.write_flash()
        self.source_marker = b""
        self.refresh_provenance()

    def write_description(self) -> None:
        (self.build / "project_description.json").write_text(json.dumps(self.description))

    def write_aec_commands(self, mode: str) -> None:
        self.aec_commands = []
        sources = [f"components/mybot_platform/src/drivers/audio/{name}" for name in
                   ("codec_audio.c", "mosaico_codec_audio.c", "mosaico_local_aec.c")]
        sources += [f"components/mybot_stack/mybot_sdk/mybot/src/{source}" for source in
                    ("media/mybot_media_pipeline.c", "core/mybot_app.c", "service/mybot_device_client.c",
                     "rtc/mybot_agora_rtc.c")]
        for source in sources:
            name = Path(source).name
            macros = f"-DMYBOT_CLOUD_AEC={int(mode == 'cloud')}"
            if not name.startswith("mybot_"):
                macros += f" -DMYBOT_LOCAL_AEC={int(mode == 'local')}"
            self.aec_commands.append({"file": f"/project/{source}", "command": f"cc {macros} -c {source}"})
        (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands))

    def select_aec_mode(self, mode: str) -> None:
        text = re.sub(r"^(?:CONFIG_MYBOT_(?:LOCAL_AEC|CLOUD_AEC|AEC_DISABLED)=.*|"
                      r"# CONFIG_MYBOT_(?:LOCAL_AEC|CLOUD_AEC|AEC_DISABLED) is not set)\n", "",
                      self.config, flags=re.MULTILINE)
        symbol = {"local": "LOCAL_AEC", "cloud": "CLOUD_AEC", "disabled": "AEC_DISABLED"}[mode]
        (self.build / "sdkconfig").write_text(text + f"CONFIG_MYBOT_{symbol}=y\n")
        self.write_aec_commands(mode)
        (self.build / "mybot_esp32s31.map").write_text(
            self.map_text if mode == "local" else self.map_text.replace(self.local_aec_live, ""))
        self.refresh_provenance()

    def write_flash(self) -> None:
        (self.build / "flasher_args.json").write_text(json.dumps(self.flash))

    def write_partition_table(self, checksum=True) -> None:
        data = b"".join(struct.pack("<2sBBII16sI", b"\xaa\x50", kind, subtype, offset, size,
                                    name.encode(), flags)
                        for name, kind, subtype, offset, size, flags in self.partitions)
        if checksum:
            data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
        data += b"\xff" * (0xC00 - len(data))
        (self.build / "partition_table/partition-table.bin").write_bytes(data)

    def resize_application(self, size: int) -> None:
        marker_size = len(self.source_marker)
        self.assertGreater(size, marker_size)
        (self.build / self.description["app_bin"]).write_bytes(b"A" * (size - marker_size))
        self.refresh_provenance()
        self.assertEqual((self.build / self.description["app_bin"]).stat().st_size, size)

    def refresh_provenance(self) -> None:
        snapshot = {
            "schema_version": 1,
            "source": {
                "revision": "a" * 40, "dirty": False, "inputs_sha256": "1" * 64,
                "input_files": 2, "coverage": "synthetic project build inputs",
            },
            "sdkconfig_sha256": provenance.sha256(self.build / "sdkconfig"),
        }
        marker = provenance.marker(snapshot)
        for name in (self.description["app_elf"], self.description["app_bin"]):
            path = self.build / name
            data = path.read_bytes()
            if self.source_marker:
                data = data.replace(self.source_marker, b"")
            path.write_bytes(data + marker)
        self.source_marker = marker
        _, names = provenance.artifact_names(self.build)
        stamp = {
            "schema_version": 1, "snapshot": snapshot,
            "artifacts": [{"file": name, "sha256": provenance.sha256(self.build / name)}
                          for name in names],
        }
        (self.build / provenance.SNAPSHOT).write_text(json.dumps(snapshot))
        (self.build / provenance.STAMP).write_text(json.dumps(stamp))
        segments = [(int(offset, 0), (self.build / name).read_bytes())
                    for offset, name in self.flash["flash_files"].items()]
        image = bytearray(b"\xff" * max(offset + len(data) for offset, data in segments))
        for offset, data in segments:
            image[offset:offset + len(data)] = data
        self.merged.write_bytes(image)

    def test_mosaico_delivery_uses_fixed_board_and_codec(self) -> None:
        (self.build / "sdkconfig").write_text(self.config.replace("ES8311", "ES8389"))
        with self.assertRaisesRegex(RuntimeError, "must not enable CONFIG_CODEC_ES8389_SUPPORT"):
            checker.verify_build(self.build, "zh-CN")
        (self.build / "sdkconfig").write_text(self.config)
        self.refresh_provenance()
        metadata = checker.package(self.build, self.merged, "zh-CN")
        self.assertEqual(metadata["board"], "esp-mosaico-v1.2")
        self.assertIn("Board: esp-mosaico-v1.2", self.merged.with_suffix(".flash.txt").read_text())

    def test_mosaico_rejects_each_other_codec_driver(self) -> None:
        for symbol in checker.FORBIDDEN_CODEC_CONFIGS:
            with self.subTest(symbol=symbol):
                (self.build / "sdkconfig").write_text(self.config + f"{symbol}=y\n")
                self.refresh_provenance()
                with self.assertRaisesRegex(RuntimeError, f"must not enable {symbol}"):
                    checker.verify_build(self.build, "zh-CN")

    def enable_mosaico_uac(self) -> None:
        (self.build / "sdkconfig").write_text(
            self.config + "CONFIG_USB_DEVICE_UAC_AS_PART=y\n"
        )
        self.usb_link_flags = (
            "-Wl,--gc-sections,--wrap=dcd_edpt_xfer,--wrap=dcd_event_handler"
            ",--wrap=usbd_edpt_xfer_fifo"
        )
        self.usb_live_symbols = "".join(
            f"                0x400a{index}000                __wrap_{symbol}\n"
            for index, symbol in enumerate(checker.USB_WRAPPED_SYMBOLS, 1)
        )
        self.usb_discarded = "Discarded input sections\n" + "".join(
            f" .text.__wrap_{symbol}\n"
            "                0x00000000       0x96 esp-idf/mybot_platform/libmybot_platform.a(usb.c.obj)\n"
            for symbol in checker.USB_WRAPPED_SYMBOLS
        )
        self.usb_cross_reference = "Cross Reference Table\n" + "".join(
            f"__wrap_{symbol}    esp-idf/mybot_platform/libmybot_platform.a(usb.c.obj)\n"
            for symbol in checker.USB_WRAPPED_SYMBOLS
        )
        self.write_usb_link_fixture()
        self.refresh_provenance()

    def write_usb_link_fixture(self, flags=None, live=None, elf_target=None) -> None:
        if flags is None:
            flags = self.usb_link_flags
        if live is None:
            live = self.usb_live_symbols
        if elf_target is None:
            elf_target = self.description["app_elf"]
        (self.build / "build.ninja").write_text(
            # A matching flag elsewhere must never satisfy the application check.
            "build unrelated.elf: CXX_EXECUTABLE_LINKER_other other.obj\n"
            f"  LINK_FLAGS = {self.usb_link_flags}\n\n"
            f"build {elf_target}: CXX_EXECUTABLE_LINKER_app main.obj | library.a\n"
            f"  LINK_FLAGS = {flags}\n"
            "  TARGET_FILE = application.elf\n\n"
        )
        (self.build / "mybot_esp32s31.map").write_text(
            self.map_text.split("Linker script and memory map\n", 1)[0] + self.usb_discarded +
            "Linker script and memory map\n" + self.local_aec_live + live + self.usb_cross_reference
        )

    def test_mosaico_uac_requires_link_flags_and_live_wrappers(self) -> None:
        self.enable_mosaico_uac()
        metadata = checker.verify_build(self.build, "zh-CN")
        self.assertEqual(metadata["board"], "esp-mosaico-v1.2")

    def test_mosaico_uac_rejects_compiling_vendor_runtime(self) -> None:
        self.enable_mosaico_uac()
        (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands + [
            {"file": "/project/managed_components/espressif__usb_device_uac/usb_device_uac.c",
             "command": "cc -c usb_device_uac.c"},
        ]))
        with self.assertRaisesRegex(RuntimeError, "must not compile.*usb_device_uac.c"):
            checker.verify_build(self.build, "zh-CN")

    def test_mosaico_uac_rejects_live_vendor_symbols_or_object(self) -> None:
        self.enable_mosaico_uac()
        vendor_records = [f"                0x40012340                {symbol}\n"
                          for symbol in ("uac_device_init", "usb_mic_task", "usb_spk_task")]
        vendor_records.extend([
            " .text.other 0x40012340 0x40 esp-idf/espressif__usb_device_uac/"
            "libespressif__usb_device_uac.a(usb_device_uac.c.obj)\n",
            " .text.other 0x40012340 0x40 vendor.a(usb_device_uac.o)\n",
        ])
        for record in vendor_records:
            with self.subTest(record=record.strip()):
                self.write_usb_link_fixture(live=self.usb_live_symbols + record)
                with self.assertRaisesRegex(RuntimeError, "must not link.*vendor UAC|must not link.*vendor usb_device_uac"):
                    checker.verify_build(self.build, "zh-CN")

    def test_mosaico_uac_allows_header_component_and_discarded_vendor_records(self) -> None:
        self.enable_mosaico_uac()
        (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands + [
            {"file": "/project/components/mybot_platform/src/drivers/usb/mosaico_usb_audio.c",
             "command": "cc -I/project/managed_components/espressif__usb_device_uac/include -c source.c"},
        ]))
        self.usb_discarded += (
            " .text.uac_device_init 0x00000000 0x40 vendor.a(usb_device_uac.c.obj)\n"
            " .text.usb_mic_task 0x00000000 0x40 vendor.a(usb_device_uac.c.obj)\n"
            " .text.usb_spk_task 0x00000000 0x40 vendor.a(usb_device_uac.c.obj)\n"
        )
        self.usb_cross_reference += "uac_device_init vendor.a(usb_device_uac.c.obj)\n"
        self.write_usb_link_fixture()
        metadata = checker.verify_build(self.build, "zh-CN")
        self.assertEqual(metadata["board"], "esp-mosaico-v1.2")

    def test_mosaico_uac_rejects_current_discarded_wrapper_failure(self) -> None:
        self.enable_mosaico_uac()
        self.write_usb_link_fixture(flags="-Wl,--gc-sections", live="")
        with self.assertRaisesRegex(RuntimeError, "link must include --wrap=dcd_edpt_xfer"):
            checker.verify_build(self.build, "zh-CN")
        # Even if flags are present, discarded/cross-reference-only names do not
        # prove the wrappers survived the final ELF link.
        self.write_usb_link_fixture(live="")
        with self.assertRaisesRegex(RuntimeError, "nonzero live map symbol"):
            checker.verify_build(self.build, "zh-CN")

    def test_mosaico_uac_rejects_each_missing_link_flag(self) -> None:
        self.enable_mosaico_uac()
        for symbol in checker.USB_WRAPPED_SYMBOLS:
            with self.subTest(symbol=symbol):
                flags = self.usb_link_flags.replace(f",--wrap={symbol}", "")
                self.write_usb_link_fixture(flags=flags)
                with self.assertRaisesRegex(RuntimeError, f"link must include --wrap={symbol}"):
                    checker.verify_build(self.build, "zh-CN")
        self.write_usb_link_fixture(elf_target="wrong-app.elf")
        with self.assertRaisesRegex(RuntimeError, "link must include --wrap"):
            checker.verify_build(self.build, "zh-CN")

    def test_mosaico_uac_rejects_each_missing_or_zero_live_symbol(self) -> None:
        self.enable_mosaico_uac()
        for symbol in checker.USB_WRAPPED_SYMBOLS:
            for zero_address in (False, True):
                with self.subTest(symbol=symbol, zero_address=zero_address):
                    lines = self.usb_live_symbols.splitlines(keepends=True)
                    live = "".join(
                        (f"                0x00000000                __wrap_{symbol}\n"
                         if zero_address else "") if f"__wrap_{symbol}" in line else line
                        for line in lines
                    )
                    self.write_usb_link_fixture(live=live)
                    with self.assertRaisesRegex(RuntimeError, f"__wrap_{symbol}.*nonzero live"):
                        checker.verify_build(self.build, "zh-CN")

    def test_disabled_es8389_comment_is_accepted_without_uac(self) -> None:
        checker.verify_build(self.build, "zh-CN")

    def test_delivery_rejects_unknown_board(self) -> None:
        (self.build / "CMakeCache.txt").write_text("MYBOT_BOARD:STRING=unknown\n")
        with self.assertRaisesRegex(RuntimeError, "unsupported or missing MYBOT_BOARD"):
            checker.verify_build(self.build, "zh-CN")

    def test_cli_reports_selected_board(self) -> None:
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["check_s31_build.py", "--build-dir", str(self.build),
                                            "--language", "zh-CN"]), contextlib.redirect_stdout(output):
            self.assertEqual(checker.main(), 0)
        self.assertIn("Validated esp-mosaico-v1.2 zh-CN", output.getvalue())

    def test_aec_modes_match_config_compile_link_and_manifest(self) -> None:
        for mode in ("local", "cloud", "disabled"):
            with self.subTest(mode=mode):
                self.select_aec_mode(mode)
                metadata = checker.package(self.build, self.merged, "zh-CN", mode)
                self.assertEqual(metadata["aec_mode"], mode)
                self.assertIn(f"AEC: {mode}", self.merged.with_suffix(".flash.txt").read_text())
                if mode == "local":
                    self.assertEqual(metadata["local_aec"], {
                        "component": "espressif/esp-sr", "version": "2.6.0",
                        "archive": "lib/esp32s31/libesp_audio_processor.a",
                    })
                else:
                    self.assertNotIn("local_aec", metadata)

    def test_aec_choice_requires_one_enabled_mode_and_matches_request(self) -> None:
        for text in (self.config.replace("CONFIG_MYBOT_LOCAL_AEC=y", "# CONFIG_MYBOT_LOCAL_AEC is not set"),
                     self.config + "CONFIG_MYBOT_CLOUD_AEC=y\n",
                     self.config + "CONFIG_MYBOT_AEC_DISABLED=y\n"):
            with self.subTest(config=text):
                (self.build / "sdkconfig").write_text(text)
                with self.assertRaisesRegex(RuntimeError, "exactly one AEC mode"):
                    checker.verify_build(self.build, "zh-CN")
        self.select_aec_mode("cloud")
        with self.assertRaisesRegex(RuntimeError, "expected local.*got cloud"):
            checker.verify_build(self.build, "zh-CN")

    def test_aec_compiler_macros_reject_missing_wrong_conflicting_and_undefined_values(self) -> None:
        for source in range(len(self.aec_commands)):
            original = self.aec_commands[source]["command"]
            for old, replacement in (("-DMYBOT_CLOUD_AEC=0", ""),
                                     ("-DMYBOT_CLOUD_AEC=0", "-DMYBOT_CLOUD_AEC=1"),
                                     ("-DMYBOT_CLOUD_AEC=0", "-DMYBOT_CLOUD_AEC=2"),
                                     ("-DMYBOT_CLOUD_AEC=0", "-DMYBOT_CLOUD_AEC=0 -DMYBOT_CLOUD_AEC=1"),
                                     ("-DMYBOT_CLOUD_AEC=0", "-DMYBOT_CLOUD_AEC=0 -UMYBOT_CLOUD_AEC")):
                with self.subTest(source=source, replacement=replacement):
                    self.aec_commands[source]["command"] = original.replace(old, replacement)
                    (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands))
                    with self.assertRaisesRegex(RuntimeError, "MYBOT_CLOUD_AEC"):
                        checker.verify_build(self.build, "zh-CN")
            self.aec_commands[source]["command"] = original
        original = self.aec_commands[0]["command"]
        for replacement in ("", "-DMYBOT_LOCAL_AEC=0", "-DMYBOT_LOCAL_AEC=true"):
            self.aec_commands[0]["command"] = original.replace("-DMYBOT_LOCAL_AEC=1", replacement)
            (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands))
            with self.assertRaisesRegex(RuntimeError, "MYBOT_LOCAL_AEC"):
                checker.verify_build(self.build, "zh-CN")

    def test_aec_compile_arguments_take_precedence_and_allow_split_defines(self) -> None:
        for command in self.aec_commands:
            command["arguments"] = shlex.split(command["command"].replace("-D", "-D "))
        (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands))
        checker.verify_build(self.build, "zh-CN")
        self.aec_commands[0]["arguments"].append("-UMYBOT_LOCAL_AEC")
        (self.build / "compile_commands.json").write_text(json.dumps(self.aec_commands))
        with self.assertRaisesRegex(RuntimeError, "MYBOT_LOCAL_AEC"):
            checker.verify_build(self.build, "zh-CN")

    def test_aec_compile_sources_must_include_actual_platform_and_sdk_units(self) -> None:
        for index in range(len(self.aec_commands)):
            with self.subTest(index=index):
                commands = self.aec_commands[:index] + self.aec_commands[index + 1:]
                commands.append({"file": "/other/" + Path(self.aec_commands[index]["file"]).name,
                                 "command": self.aec_commands[index]["command"]})
                (self.build / "compile_commands.json").write_text(json.dumps(commands))
                with self.assertRaisesRegex(RuntimeError, "missing AEC compile command"):
                    checker.verify_build(self.build, "zh-CN")

    def test_local_aec_requires_live_nonzero_owned_functions(self) -> None:
        live_symbol = "                0x40010300                aec_process\n"
        variants = (
            self.map_text.replace(live_symbol, ""),
            self.map_text.replace(live_symbol, live_symbol.replace("0x40010300", "0x00000000")),
            self.map_text.replace("0x40010300       0x40", "0x40010300       0x0"),
            self.map_text.replace(live_symbol, live_symbol.replace("0x40010300", "0x40010400")),
            self.map_text.replace("lib/esp32s31/", "lib/esp32s3/"),
            self.map_text.replace("lib/esp32s31/", "lib/esp32p4/"),
            self.map_text.replace("libmybot_platform.a(mosaico_local_aec.c.obj)", "other.a(fake.obj)"),
        )
        for text in variants:
            with self.subTest(map=text[-250:]):
                (self.build / "mybot_esp32s31.map").write_text(text)
                with self.assertRaisesRegex(RuntimeError, "local AEC.*live|local AEC.*origin"):
                    checker.verify_build(self.build, "zh-CN")
        for symbol in ("mosaico_local_aec_create", "mosaico_local_aec_feed", "aec_create_from_config", "aec_process"):
            with self.subTest(missing=symbol):
                text = re.sub(rf"^[ \t]+0x[0-9a-fA-F]+[ \t]+{symbol}\n", "", self.map_text, flags=re.MULTILINE)
                (self.build / "mybot_esp32s31.map").write_text(text)
                with self.assertRaisesRegex(RuntimeError, "local AEC.*live"):
                    checker.verify_build(self.build, "zh-CN")

    def test_local_aec_load_discarded_and_cross_reference_records_are_not_live(self) -> None:
        prefix = self.map_text.split("Linker script and memory map\n", 1)[0]
        for text in (
            prefix + "Discarded input sections\n" + self.local_aec_live + "Linker script and memory map\n",
            prefix + "Linker script and memory map\nCross Reference Table\n" + self.local_aec_live,
            prefix + "Linker script and memory map\nLOAD /project/managed_components/espressif__esp-sr/"
            "lib/esp32s31/libesp_audio_processor.a\n",
        ):
            with self.subTest(map=text[-100:]):
                (self.build / "mybot_esp32s31.map").write_text(text)
                with self.assertRaisesRegex(RuntimeError, "local AEC.*live"):
                    checker.verify_build(self.build, "zh-CN")

    def test_other_aec_modes_reject_live_local_symbols_but_allow_discarded_and_load(self) -> None:
        for mode in ("cloud", "disabled"):
            self.select_aec_mode(mode)
            base = self.map_text.replace(self.local_aec_live, "")
            discarded = (base.split("Linker script and memory map\n", 1)[0] +
                         "Discarded input sections\n" + self.local_aec_live +
                         "Linker script and memory map\nLOAD /project/managed_components/espressif__esp-sr/"
                         "lib/esp32s31/libesp_audio_processor.a\nCross Reference Table\n" + self.local_aec_live)
            (self.build / "mybot_esp32s31.map").write_text(discarded)
            checker.verify_build(self.build, "zh-CN", mode)
            for symbol in ("mosaico_local_aec_feed", "aec_process", "aec_destroy"):
                with self.subTest(mode=mode, symbol=symbol):
                    (self.build / "mybot_esp32s31.map").write_text(
                        base + f"                0x40012340                {symbol}\n")
                    with self.assertRaisesRegex(RuntimeError, "must not link live local AEC"):
                        checker.verify_build(self.build, "zh-CN", mode)
            for owner in ("libmybot_platform.a(mosaico_local_aec.c.obj)",
                          "libesp_audio_processor.a(esp_aec.c.obj)"):
                with self.subTest(mode=mode, owner=owner):
                    (self.build / "mybot_esp32s31.map").write_text(
                        base + f" .text.internal_function 0x40012340 0x40 {owner}\n")
                    with self.assertRaisesRegex(RuntimeError, "must not link live local AEC object"):
                        checker.verify_build(self.build, "zh-CN", mode)

    def test_vendor_only_rejects_aec_mode_argument(self) -> None:
        with mock.patch.object(sys, "argv", ["check_s31_build.py", "--vendor-only", "--aec-mode", "cloud"]), \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as caught:
                checker.main()
        self.assertEqual(caught.exception.code, 2)

    def test_aec_cli_rejects_stale_mode_and_accepts_explicit_cloud(self) -> None:
        self.select_aec_mode("cloud")
        args = ["check_s31_build.py", "--build-dir", str(self.build), "--language", "zh-CN"]
        with mock.patch.object(sys, "argv", args):
            with self.assertRaisesRegex(RuntimeError, "expected local.*got cloud"):
                checker.main()
        with mock.patch.object(sys, "argv", args + ["--aec-mode", "cloud"]), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(checker.main(), 0)

    def test_delivery_has_exact_offsets_hashes_versions_and_no_vendor_inputs(self) -> None:
        metadata = checker.package(self.build, self.merged, "zh-CN")
        manifest = json.loads(self.merged.with_suffix(".manifest.json").read_text())
        self.assertEqual(metadata, manifest)
        self.assertEqual(manifest["image"]["flash_offset"], "0x0")
        self.assertEqual(manifest["image"]["sha256"], hashlib.sha256(self.merged.read_bytes()).hexdigest())
        self.assertEqual([s["offset"] for s in manifest["segments"]], ["0x2000", "0x8000", "0x20000"])
        self.assertEqual(manifest["project_version"], "0.1.0")
        self.assertEqual(manifest["idf_version"], "v6.1")
        self.assertFalse(manifest["video_enabled"])
        self.assertEqual(manifest["source"]["revision"], "a" * 40)
        self.assertFalse(manifest["source"]["dirty"])
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
            self.merged.with_suffix(".licenses.txt").name,
        })

    def test_delivery_keeps_complete_original_license_and_notice_bytes(self) -> None:
        checker.package(self.build, self.merged, "zh-CN")
        payload = self.merged.with_suffix(".licenses.txt").read_bytes()
        for name in ("THIRD_PARTY_NOTICES.md", "LICENSE", "licenses/esp-sr-2.6.0.LICENSE",
                     "licenses/esp-dl-3.3.15.LICENSE", "licenses/esp_new_jpeg-1.0.2.LICENSE",
                     "components/mybot_platform/src/drivers/display/assets/licenses/LVGL_VIEW_FONT_LICENSE.txt"):
            with self.subTest(name=name):
                original = (ROOT / name).read_bytes()
                self.assertIn(name.encode(), payload)
                self.assertIn(original, payload)
        self.assertIn(b"Permission is hereby granted for use on all ESPRESSIF SYSTEMS products", payload)
        self.assertIn(b"Apache License", payload)
        self.assertIn(b"Copyright (c) 2021 Espressif Systems", payload)
        self.assertIn(b"SIL OPEN FONT LICENSE Version 1.1", payload)

    def test_license_output_symlink_is_rejected_without_writing_sidecars(self) -> None:
        target = self.root / "preserve.txt"
        target.write_bytes(b"preserve")
        self.merged.with_suffix(".licenses.txt").symlink_to(target)
        with self.assertRaisesRegex(RuntimeError, "symlink output"):
            checker.package(self.build, self.merged, "zh-CN")
        self.assertEqual(target.read_bytes(), b"preserve")
        for suffix in (".manifest.json", ".sha256", ".flash.txt"):
            self.assertFalse(self.merged.with_suffix(suffix).exists())

    def test_missing_license_fails_before_writing_delivery_sidecars(self) -> None:
        read_bytes = Path.read_bytes

        def missing_license(path):
            if path == ROOT / "licenses/esp-sr-2.6.0.LICENSE":
                raise FileNotFoundError(path)
            return read_bytes(path)

        with mock.patch.object(Path, "read_bytes", missing_license):
            with self.assertRaises(FileNotFoundError):
                checker.package(self.build, self.merged, "zh-CN")
        for suffix in (".manifest.json", ".sha256", ".flash.txt", ".licenses.txt"):
            self.assertFalse(self.merged.with_suffix(suffix).exists())

    def test_packaging_uses_frozen_build_source_without_querying_checkout(self) -> None:
        # Packaging may happen after a checkout change. Only the completed
        # artifact-bound stamp can identify the source of these firmware bytes.
        with mock.patch.object(provenance.subprocess, "run",
                               side_effect=AssertionError("packaging must not query live Git")):
            metadata = checker.package(self.build, self.merged, "zh-CN")
        self.assertEqual(metadata["source"]["revision"], "a" * 40)
        self.assertFalse(metadata["source"]["dirty"])

    def test_capacity_uses_every_built_ota_slot_and_accepts_exact_300kib_margin(self) -> None:
        name, kind, subtype, offset, _, flags = self.partitions[4]
        smaller_slot = 0x3E0000
        self.partitions[4] = (name, kind, subtype, offset, smaller_slot, flags)
        self.write_partition_table()
        self.resize_application(smaller_slot - 300 * 1024)
        metadata = checker.verify_build(self.build, "zh-CN")
        capacity = metadata["ota_capacity"]
        self.assertEqual(capacity["minimum_required_free_bytes"], 307200)
        self.assertEqual(capacity["minimum_free_bytes"], 307200)
        self.assertEqual(capacity["flashed_partition"], "ota_0")
        self.assertEqual([slot["name"] for slot in capacity["slots"]], ["ota_0", "ota_1"])
        self.assertGreater(capacity["slots"][0]["free_bytes"], 307200)
        self.resize_application(smaller_slot - 300 * 1024 + 1)
        with self.assertRaisesRegex(RuntimeError, "ota_1 has 307199 free bytes"):
            checker.verify_build(self.build, "zh-CN")

    def test_application_that_only_fits_active_slot_still_rejects_inactive_overflow(self) -> None:
        name, kind, subtype, offset, _, flags = self.partitions[4]
        self.partitions[4] = (name, kind, subtype, offset, 0x200000, flags)
        self.write_partition_table()
        self.resize_application(0x210000)
        with self.assertRaisesRegex(RuntimeError, "OTA partition ota_1 has -65536 free bytes"):
            checker.verify_build(self.build, "zh-CN")

    def test_application_overflow_or_zero_margin_is_rejected(self) -> None:
        self.resize_application(0x3F0001)
        with self.assertRaisesRegex(RuntimeError, "application exceeds its flashed partition"):
            checker.verify_build(self.build, "zh-CN")
        self.resize_application(0x3F0000)
        with self.assertRaisesRegex(RuntimeError, "ota_0 has 0 free bytes"):
            checker.verify_build(self.build, "zh-CN")

    def test_partition_capacity_uses_flash_records_and_binary_not_csv_or_fixed_filename(self) -> None:
        original_read = Path.read_text

        def forbid_csv(path, *args, **kwargs):
            self.assertNotEqual(path.suffix, ".csv", "delivery must use the built binary table")
            return original_read(path, *args, **kwargs)

        table = self.build / "partition_table/partition-table.bin"
        table.rename(self.build / "compiled-layout.bin")
        self.flash["flash_files"]["0x8000"] = "compiled-layout.bin"
        self.flash["partition-table"]["file"] = "compiled-layout.bin"
        self.write_flash()
        self.refresh_provenance()
        with mock.patch.object(Path, "read_text", forbid_csv):
            metadata = checker.verify_build(self.build, "zh-CN")
        self.assertEqual([slot["size"] for slot in metadata["ota_capacity"]["slots"]],
                         [0x3F0000, 0x3F0000])

    def test_app_flash_offset_must_be_partition_start_and_application_type(self) -> None:
        for offset in (0x21000, 0x9000):
            with self.subTest(offset=hex(offset)):
                self.flash["flash_files"].pop(self.flash["app"]["offset"])
                self.flash["app"]["offset"] = hex(offset)
                self.flash["flash_files"][hex(offset)] = self.description["app_bin"]
                self.write_flash()
                with self.assertRaisesRegex(RuntimeError, "must match an application partition start"):
                    checker.verify_build(self.build, "zh-CN")

    def test_missing_or_inconsistent_app_and_partition_flash_records_are_rejected(self) -> None:
        for role in ("app", "partition-table"):
            original = self.flash[role]
            for value in (None, {"offset": "bad", "file": original["file"]},
                          {"offset": hex(int(original["offset"], 0) + 0x1000), "file": original["file"]},
                          {"offset": original["offset"], "file": "another.bin"}):
                with self.subTest(role=role, value=value):
                    if value is None:
                        self.flash.pop(role)
                    else:
                        self.flash[role] = value
                    self.write_flash()
                    with self.assertRaisesRegex(RuntimeError, "flash record"):
                        checker.verify_build(self.build, "zh-CN")
                    self.flash[role] = original

    def test_malformed_partition_binary_and_checksum_are_rejected(self) -> None:
        path = self.build / "partition_table/partition-table.bin"
        original = path.read_bytes()
        md5_offset = len(self.partitions) * 32
        corrupted_md5 = bytearray(original)
        corrupted_md5[md5_offset + 16] ^= 1
        bad_md5_header = bytearray(original)
        bad_md5_header[md5_offset + 2] = 0
        after_end = bytearray(original)
        after_end[md5_offset + 64] = 0
        bad_label = bytearray(original)
        bad_label[12] = 0xFF
        variants = (
            b"", original[:-1], original + b"\xff" * 32, original[:md5_offset],
            b"\x00\x00" + original[2:], bytes(corrupted_md5), bytes(bad_md5_header),
            original[:md5_offset + 32] + original[md5_offset:md5_offset + 32] +
            b"\xff" * (0xC00 - md5_offset - 64),
            original[:md5_offset + 32] + original[:32] + b"\xff" * (0xC00 - md5_offset - 64),
            bytes(after_end), bytes(bad_label), b"\xff" * 0xC00,
        )
        for table in variants:
            with self.subTest(length=len(table), first=table[:8]):
                with self.assertRaisesRegex(RuntimeError, "partition|Partition"):
                    checker.parse_partition_table(table, 0x8000)
                path.write_bytes(table)
                with self.assertRaises(RuntimeError):
                    checker.verify_build(self.build, "zh-CN")

    def test_malformed_partition_bounds_overlap_flags_and_duplicate_slots_are_rejected(self) -> None:
        original = list(self.partitions)
        variants = (
            (4, ("ota_1", 0, 0x11, 0x410000, 0, 0)),
            (4, ("ota_1", 0, 0x11, 0x410001, 0x3F0000, 0)),
            (4, ("ota_1", 0, 0x11, 0x410000, 0x3F0001, 0)),
            (4, ("ota_1", 0, 0x11, 0xFF0000, 0x20000, 0)),
            (4, ("ota_1", 0, 0x11, 0x410000, 0xFFFFF000, 0)),
            (4, ("ota_1", 0, 0x11, 0x400000, 0x3F0000, 0)),
            (4, ("ota_1", 0, 0x10, 0x410000, 0x3F0000, 0)),
            (4, ("ota_0", 0, 0x11, 0x410000, 0x3F0000, 0)),
            (4, ("ota_1", 0, 0x11, 0x410000, 0x3F0000, 2)),
            (4, ("ota_1", 0, 0x11, 0x410000, 0x3F0000, 4)),
            (0, ("nvs", 1, 2, 0x8000, 0x4000, 0)),
        )
        for index, entry in variants:
            with self.subTest(entry=entry):
                self.partitions = list(original)
                self.partitions[index] = entry
                self.write_partition_table()
                with self.assertRaisesRegex(RuntimeError, "partition|OTA"):
                    checker.parse_partition_table(
                        (self.build / "partition_table/partition-table.bin").read_bytes(), 0x8000)
                with self.assertRaises(RuntimeError):
                    checker.verify_build(self.build, "zh-CN")

    def test_partition_table_flash_offset_bounds_and_alignment_are_rejected(self) -> None:
        table = (self.build / "partition_table/partition-table.bin").read_bytes()
        for offset in (-0x1000, 0x8001, 16 * 1024 * 1024):
            with self.subTest(offset=offset):
                with self.assertRaisesRegex(RuntimeError, "invalid partition table flash offset"):
                    checker.parse_partition_table(table, offset)

    def test_factory_test_and_tee_apps_are_not_normal_ota_slots(self) -> None:
        name, kind, _, offset, size, flags = self.partitions[4]
        for subtype in (0x00, 0x20, 0x30, 0x31):
            with self.subTest(subtype=subtype):
                self.partitions[4] = (name, kind, subtype, offset, size, flags)
                self.write_partition_table()
                self.refresh_provenance()
                metadata = checker.verify_build(self.build, "zh-CN")
                self.assertEqual([slot["name"] for slot in metadata["ota_capacity"]["slots"]], ["ota_0"])
        self.partitions[3] = ("factory", 0, 0x00, 0x20000, 0x3F0000, 0)
        self.write_partition_table()
        with self.assertRaisesRegex(RuntimeError, "contain writable OTA application slots"):
            checker.verify_build(self.build, "zh-CN")

    def test_standard_table_without_optional_md5_is_accepted(self) -> None:
        self.write_partition_table(checksum=False)
        self.refresh_provenance()
        self.assertEqual(len(checker.verify_build(self.build, "zh-CN")["ota_capacity"]["slots"]), 2)

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
        self.flash["extra_esptool_args"]["stub"] = True
        self.write_flash()
        self.refresh_provenance()
        self.merged.rename(image)
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
    def test_ci_trusts_only_checked_out_workspace_before_idf_runs(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        build_job = workflow.split("  idf-build:", 1)[1]
        trusted = build_job.index("      - name: Allow Git access to checked-out workspace\n")
        self.assertLess(build_job.index("      - uses: actions/checkout@v4\n"), trusted)
        self.assertLess(trusted, build_job.index("      - name: Verify toolchain\n"))
        self.assertLess(trusted, build_job.index("      - name: Build and package translation firmware\n"))
        body = build_job[trusted:].split("        run: |\n", 1)[1].split("      - ", 1)[0]
        self.assertIn('git config --global --add safe.directory "$GITHUB_WORKSPACE"', body)
        self.assertIn('git -C "$GITHUB_WORKSPACE" rev-parse --verify HEAD', body)
        self.assertNotIn("*", body)

    def test_ci_builds_both_languages_for_the_fixed_board(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        build_job = workflow.split("  idf-build:", 1)[1]
        matrix = build_job.split("      matrix:\n", 1)[1].split("    steps:\n", 1)[0]
        self.assertEqual(re.findall(r"- language: (\S+)\s+aec_mode: (\S+)", matrix),
                         [("zh-CN", "local"), ("en-US", "local"), ("zh-CN", "cloud")])
        self.assertNotIn("-DMYBOT_BOARD", build_job)
        self.assertIn(
            'python3 scripts/build_all.py --language "${{ matrix.language }}"',
            build_job,
        )
        self.assertIn('--aec-mode "${{ matrix.aec_mode }}"', build_job)
        self.assertIn('--build-root "$RUNNER_TEMP/mybot-build"', build_job)
        self.assertIn('--output-root "$GITHUB_WORKSPACE/releases"', build_job)
        self.assertIn(
            'mybot-esp-mosaico-v1.2-${{ matrix.language }}-aec-${{ matrix.aec_mode }}-ptime60-no-video-',
            build_job,
        )
        descriptor_step = build_job.split(
            "      - name: Verify composite descriptors against pinned USB headers\n", 1
        )[1].split("      - name:", 1)[0]
        self.assertLess(
            build_job.index("      - name: Build and package translation firmware\n"),
            build_job.index("      - name: Verify composite descriptors against pinned USB headers\n"),
        )
        self.assertNotIn("matrix.board", descriptor_step)
        self.assertIn("run: sh tests/mosaico_usb/run_descriptors.sh", descriptor_step)

    def test_ci_merge_output_is_absolute_and_uploads_only_delivery_files(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        build_job = workflow.split("  idf-build:", 1)[1]
        self.assertNotIn("idf.py --preview -B", build_job)
        self.assertNotIn("merge-bin", build_job)
        self.assertNotIn("scripts/check_s31_build.py --build-dir", build_job)
        self.assertIn("python3 scripts/build_all.py", build_job)
        upload = workflow.split("      - name: Upload firmware", 1)[1]
        for suffix in (".bin", ".manifest.json", ".sha256", ".flash.txt", ".licenses.txt"):
            self.assertIn(
                'releases/esp-mosaico-v1.2-${{ matrix.language }}' + suffix,
                upload,
            )
        self.assertNotIn(".map", upload)
        self.assertNotIn(".tgz", upload)
        self.assertNotIn("vendor", upload)
        self.assertIn("scripts/check_s31_build.py --vendor-only", workflow)
        self.assertNotIn("rtsa_archive_url", workflow)
        self.assertNotIn("RTSA_S31_DOWNLOAD_TOKEN", workflow)
        self.assertNotIn("if: github.event_name == 'workflow_dispatch'", workflow)


if __name__ == "__main__":
    unittest.main()
