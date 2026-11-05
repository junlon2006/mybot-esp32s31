#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host-only probe checks; fake serial objects never open a physical device."""
import contextlib
import errno
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_usb_cdc_probe", ROOT / "scripts/check_usb_cdc_probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


def port_info(device="/dev/cu.usbmodem123", **overrides):
    fields = dict(device=device, vid=probe.VID, pid=probe.PID,
                  product=probe.PRODUCT, serial_number=probe.SERIAL_NUMBER)
    fields.update(overrides)
    return SimpleNamespace(**fields)


class Clock:
    def __init__(self):
        self.value = 0.0

    def now(self):
        return self.value

    def sleep(self, seconds):
        assert seconds >= 0
        self.value += seconds


class EchoSerial:
    def __init__(self, clock, write_limit=17, read_limit=11, capacity=47,
                 corrupt=False, duplicate=False, stuck=False, fail_after=None):
        self.clock = clock
        self.write_limit = write_limit
        self.read_limit = read_limit
        self.capacity = capacity
        self.corrupt = corrupt
        self.duplicate = duplicate
        self.stuck = stuck
        self.fail_after = fail_after
        self.pending = bytearray()
        self.writes = []
        self.read_calls = 0
        self.write_calls = 0
        self.total_written = 0
        self.flushes = []
        self.is_open = False
        self.closed = False
        self.dtr_changes = []
        self.port = None

    @property
    def dtr(self):
        return self.dtr_changes[-1] if self.dtr_changes else False

    @dtr.setter
    def dtr(self, value):
        self.dtr_changes.append(value)

    def open(self):
        assert not self.dtr
        self.is_open = True

    def close(self):
        self.closed = True
        self.is_open = False

    def reset_input_buffer(self):
        assert self.total_written == 0
        self.flushes.append("input")
        self.pending.clear()

    def reset_output_buffer(self):
        assert self.total_written == 0
        self.flushes.append("output")

    def write(self, data):
        self.clock.value += 0.00001
        self.write_calls += 1
        if self.fail_after is not None and self.total_written >= self.fail_after:
            raise OSError("device disconnected")
        if self.stuck or self.write_calls % 5 == 0:
            return 0
        count = min(len(data), self.write_limit, self.capacity - len(self.pending))
        accepted = data[:count]
        self.writes.append(bytes(accepted))
        self.total_written += count
        self.pending.extend(accepted)
        if self.corrupt and count:
            self.pending[-1] ^= 0xFF
        if self.duplicate and count:
            self.pending.extend(accepted)
        return count

    def read(self, count):
        self.clock.value += 0.00001
        self.read_calls += 1
        if self.read_calls % 7 == 0:
            return b""
        count = min(count, self.read_limit, len(self.pending))
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result


class HostProbeTests(unittest.TestCase):
    def test_selection_requires_complete_identity_and_native_path(self):
        self.assertTrue(probe.matches_probe(port_info()))
        self.assertTrue(probe.matches_probe(port_info("/dev/ttyACM0")))
        for fields in (
            {"device": "/dev/cu.usbserial-14410"},
            {"device": "/dev/ttyUSB0"},
            {"device": "/dev/cu.usbmodem123/other"},
            {"vid": 0x10C4}, {"pid": 0xEA60},
            {"product": "CP2102N USB to UART Bridge Controller"},
            {"product": None}, {"serial_number": "another-board"},
        ):
            with self.subTest(fields=fields):
                candidate = port_info(**fields)
                self.assertFalse(probe.matches_probe(candidate))
                with self.assertRaises(probe.ProbeError):
                    probe.select_probe([candidate], candidate.device)

    def test_selection_rejects_zero_multiple_and_missing_explicit_port(self):
        first, second = port_info(), port_info("/dev/cu.usbmodem456")
        self.assertIs(probe.select_probe([first]), first)
        self.assertIs(probe.select_probe([first, second], second.device), second)
        for candidates, requested in (([], None), ([first, second], None),
                                      ([first], second.device)):
            with self.assertRaises(probe.ProbeError):
                probe.select_probe(candidates, requested)

    def test_list_never_opens_a_port(self):
        serial_factory = mock.Mock(side_effect=AssertionError("must not open"))
        ports = [port_info(), port_info("/dev/cu.usbserial-14410")]
        output = io.StringIO()
        with mock.patch.object(probe, "load_pyserial", return_value=(serial_factory, lambda: ports)):
            with contextlib.redirect_stdout(output):
                self.assertEqual(probe.main(["--list"]), 0)
        serial_factory.assert_not_called()
        self.assertIn("/dev/cu.usbmodem123", output.getvalue())
        self.assertNotIn("usbserial", output.getvalue())

    def test_run_rejects_wrong_identity_before_open(self):
        factory = mock.Mock()
        with self.assertRaises(probe.ProbeError):
            probe.run_probe(port_info(product="another device"), factory)
        factory.assert_not_called()

    def test_challenges_cover_control_bytes_and_packet_boundaries(self):
        for size in (63, 64, 65, 511, 512, 513, 65536):
            self.assertIn(size, probe.CHALLENGE_SIZES)
            payload = probe.make_challenge(size)
            self.assertEqual(len(payload), size)
            self.assertEqual(payload[:len(probe.CONTROL_BYTES)], probe.CONTROL_BYTES)
        self.assertNotEqual(probe.make_challenge(65536), probe.make_challenge(65536))

    def test_partial_reads_writes_and_backpressure_preserve_all_bytes(self):
        clock = Clock()
        port = EchoSerial(clock)
        payload = probe.make_challenge(65536)
        probe.exchange(port, payload, 5.0, clock.now, clock.sleep)
        self.assertEqual(b"".join(port.writes), payload)
        self.assertEqual(port.pending, b"")
        self.assertGreater(port.write_calls, len(payload) // port.write_limit)
        self.assertEqual(port.flushes, [])

    def test_native_write_is_one_attempt_even_when_backpressured(self):
        with mock.patch.object(probe.os, "write", return_value=2) as write:
            self.assertEqual(probe.write_once(99, b"abcd"), 2)
            write.assert_called_once_with(99, b"abcd")
        for error_number in (errno.EAGAIN, errno.EINTR):
            with mock.patch.object(probe.os, "write", side_effect=OSError(error_number, "retry")) as write:
                self.assertEqual(probe.write_once(99, b"abcd"), 0)
                write.assert_called_once()
        with mock.patch.object(probe.os, "write", side_effect=OSError(errno.EIO, "gone")):
            with self.assertRaises(OSError):
                probe.write_once(99, b"abcd")

    def test_corruption_fails_without_searching_or_flushing(self):
        clock = Clock()
        port = EchoSerial(clock, corrupt=True)
        with self.assertRaisesRegex(probe.ProbeError, "mismatch"):
            probe.exchange(port, probe.make_challenge(65), 1.0, clock.now, clock.sleep)
        self.assertEqual(port.flushes, [])

    def test_duplicate_bytes_fail(self):
        clock = Clock()
        port = EchoSerial(clock, duplicate=True)
        with self.assertRaises(probe.ProbeError):
            probe.exchange(port, probe.make_challenge(64), 1.0, clock.now, clock.sleep)

    def test_trailing_bytes_fail_even_after_expected_payload(self):
        clock = Clock()
        port = EchoSerial(clock, write_limit=1024, read_limit=1024)
        original_read = port.read
        reads = 0

        def delayed_extra(count):
            nonlocal reads
            reads += 1
            if reads == 3:
                return b"stale"
            return original_read(count)

        port.read = delayed_extra
        with self.assertRaisesRegex(probe.ProbeError, "trailing"):
            probe.exchange(port, b"abcd", 1.0, clock.now, clock.sleep)

    def test_read_completion_past_quiet_boundary_never_sleeps_negative(self):
        clock = Clock()
        port = EchoSerial(clock)
        original_read = port.read

        def delayed_read(count):
            result = original_read(count)
            if not result:
                clock.value += probe.QUIET_SECONDS * 2
            return result

        port.read = delayed_read
        probe.exchange(port, b"abcd", 1.0, clock.now, clock.sleep)

    def test_stale_bytes_at_start_are_not_discarded_to_find_the_challenge(self):
        clock = Clock()
        port = EchoSerial(clock)
        port.pending.extend(b"stale:")
        with self.assertRaisesRegex(probe.ProbeError, "mismatch|extra"):
            probe.exchange(port, b"fresh-random-challenge", 1.0, clock.now, clock.sleep)
        self.assertEqual(port.flushes, [])

    def test_stalled_link_expires_one_monotonic_deadline(self):
        clock = Clock()
        port = EchoSerial(clock, stuck=True)
        with self.assertRaisesRegex(probe.ProbeError, "timeout"):
            probe.exchange(port, b"challenge", 0.01, clock.now, clock.sleep)
        self.assertGreaterEqual(clock.value, 0.01)
        self.assertLess(clock.value, 0.012)

    def test_progress_does_not_extend_the_deadline(self):
        clock = Clock()
        port = EchoSerial(clock, write_limit=1, read_limit=1)
        with self.assertRaisesRegex(probe.ProbeError, "timeout"):
            probe.exchange(port, probe.make_challenge(65536), 0.02, clock.now, clock.sleep)
        self.assertGreater(port.total_written, 0)
        self.assertLess(port.total_written, 65536)

    def test_dtr_reopen_rounds_reset_only_before_challenges(self):
        clock = Clock()
        opened = []

        def factory(**options):
            self.assertIsNone(options["port"])
            self.assertEqual(options["timeout"], 0)
            self.assertEqual(options["write_timeout"], 0)
            self.assertFalse(options["xonxoff"] or options["rtscts"] or options["dsrdtr"])
            self.assertTrue(options["exclusive"])
            port = EchoSerial(clock)
            port.pending.extend(b"old session")
            opened.append(port)
            return port

        result = probe.run_probe(port_info(), factory, 2, 10, clock.now, clock.sleep,
                                 report=lambda unused: None)
        self.assertEqual(len(opened), 2)
        self.assertEqual(result["bytes"], 2 * sum(probe.CHALLENGE_SIZES))
        for port in opened:
            self.assertTrue(port.closed)
            self.assertEqual(port.dtr_changes, [False, True, False])
            self.assertEqual(port.flushes, ["input", "output"])
            self.assertEqual(port.port, "/dev/cu.usbmodem123")

    def test_all_rounds_share_the_same_timeout(self):
        clock = Clock()
        opened = []

        def factory(**unused):
            port = EchoSerial(clock, write_limit=1024, read_limit=1024, capacity=4096)
            opened.append(port)
            return port

        with self.assertRaisesRegex(probe.ProbeError, "timeout"):
            probe.run_probe(port_info(), factory, 20, 1.0, clock.now, clock.sleep,
                            report=lambda unused: None)
        self.assertLess(len(opened), 20)
        self.assertTrue(all(port.closed for port in opened))

    def test_disconnect_closes_handle_and_does_not_report_success(self):
        clock = Clock()
        port = EchoSerial(clock, fail_after=20)
        with self.assertRaises(OSError):
            probe.run_probe(port_info(), lambda **unused: port, 2, 10, clock.now, clock.sleep,
                            report=lambda unused: None)
        self.assertTrue(port.closed)


if __name__ == "__main__":
    unittest.main()
