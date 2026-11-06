#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the native USB CDC probe without opening the board's UART bridge.

Run with the ESP-IDF Python environment, which already provides pyserial.
Passing proves this CDC bulk echo path only, not UAC audio or electrical safety.
"""
from __future__ import annotations

import argparse
import errno
import math
import os
import re
import secrets
import sys
import time


PRODUCT = "MyBot S31 USB Probe"
SERIAL_NUMBER = "S31-CDC-PROBE"
VID = 0x303A
PID = 0x4001
PORT_PATTERN = re.compile(r"(?:/dev/cu\.usbmodem[^/]+|/dev/ttyACM[0-9]+)\Z")
CHALLENGE_SIZES = (1, 7, 63, 64, 65, 511, 512, 513, 65536)
CONTROL_BYTES = b"\x00\r\n\x11\x13\xff\x80"
IO_CHUNK = 1024
MAX_IN_FLIGHT = 4096
IDLE_PAUSE = 0.001
QUIET_SECONDS = 0.05
DTR_SETTLE_SECONDS = 0.1


class ProbeError(RuntimeError):
    """A selection, connection or byte-integrity check failed."""


def matches_probe(port) -> bool:
    return (
        PORT_PATTERN.fullmatch(getattr(port, "device", "") or "") is not None
        and getattr(port, "vid", None) == VID
        and getattr(port, "pid", None) == PID
        and getattr(port, "product", None) == PRODUCT
        and getattr(port, "serial_number", None) == SERIAL_NUMBER
    )


def select_probe(ports, requested_port=None):
    """Even an explicit path must have the probe's complete USB identity."""
    ports = list(ports)
    if requested_port is not None:
        if PORT_PATTERN.fullmatch(requested_port) is None:
            raise ProbeError("refusing non-native-CDC port: " + requested_port)
        candidates = [port for port in ports if port.device == requested_port]
        if len(candidates) != 1 or not matches_probe(candidates[0]):
            raise ProbeError(
                "requested port does not have the expected product, serial and VID/PID: "
                + requested_port
            )
        return candidates[0]
    candidates = [port for port in ports if matches_probe(port)]
    if not candidates:
        raise ProbeError("no matching native USB CDC probe; use --list to inspect candidates")
    if len(candidates) != 1:
        raise ProbeError(
            "multiple matching probes; select one with --port: "
            + ", ".join(port.device for port in candidates)
        )
    return candidates[0]


def make_challenge(size: int) -> bytes:
    data = bytearray(secrets.token_bytes(size))
    if size >= len(CONTROL_BYTES):
        data[: len(CONTROL_BYTES)] = CONTROL_BYTES
    return bytes(data)


def remaining(deadline, clock):
    available = deadline - clock()
    if available <= 0:
        raise ProbeError("total probe timeout expired")
    return available


def wait_bounded(seconds, deadline, clock, pause):
    if remaining(deadline, clock) < seconds:
        raise ProbeError("total probe timeout expired during connection settling")
    pause(seconds)
    remaining(deadline, clock)


def exchange(port, payload, deadline, clock=time.monotonic, pause=time.sleep):
    """Verify one byte stream using the caller's unchanged overall deadline."""
    sent = received = 0
    while sent < len(payload) or received < len(payload):
        try:
            remaining(deadline, clock)
        except ProbeError as error:
            raise ProbeError(
                "total probe timeout: sent=%d received=%d expected=%d"
                % (sent, received, len(payload))
            ) from error
        progressed = False
        capacity = MAX_IN_FLIGHT - (sent - received)
        count = min(IO_CHUNK, len(payload) - sent, capacity)
        if count > 0:
            written = port.write(payload[sent : sent + count])
            if not isinstance(written, int) or written < 0 or written > count:
                raise ProbeError("serial write returned an invalid byte count")
            sent += written
            progressed = written != 0

        # The extra byte detects an immediately trailing duplicate or unsolicited byte.
        data = port.read(min(IO_CHUNK, len(payload) - received + 1))
        if not isinstance(data, bytes):
            raise ProbeError("serial read did not return bytes")
        if data:
            if len(data) > sent - received:
                raise ProbeError("unexpected extra bytes at receive offset %d" % received)
            expected = payload[received : received + len(data)]
            if data != expected:
                offset = next(
                    index for index, pair in enumerate(zip(data, expected)) if pair[0] != pair[1]
                )
                raise ProbeError("echo mismatch at byte %d" % (received + offset))
            received += len(data)
            progressed = True
        if not progressed:
            pause(min(IDLE_PAUSE, remaining(deadline, clock)))

    # Do not flush or discard unexpected input after the challenge has started.
    quiet_until = clock() + QUIET_SECONDS
    while clock() < quiet_until:
        remaining(deadline, clock)
        extra = port.read(IO_CHUNK)
        if not isinstance(extra, bytes):
            raise ProbeError("serial read did not return bytes")
        if extra:
            raise ProbeError("unexpected trailing bytes after %d-byte challenge" % len(payload))
        # Reading can pass the quiet boundary; never send a negative duration to sleep().
        pause(min(IDLE_PAUSE, max(0.0, quiet_until - clock()), remaining(deadline, clock)))


def run_probe(port_info, serial_factory, rounds=2, timeout=30.0,
              clock=time.monotonic, pause=time.sleep, report=print):
    if not matches_probe(port_info):
        raise ProbeError("port identity must be checked before opening it")
    if rounds < 1 or not math.isfinite(timeout) or timeout <= 0:
        raise ProbeError("rounds and timeout must be positive")
    started = clock()
    deadline = started + timeout
    verified = 0
    for round_number in range(1, rounds + 1):
        remaining(deadline, clock)
        # Configure DTR before opening; do not open the UART console or enable flow control.
        port = serial_factory(
            port=None, baudrate=115200, timeout=0, write_timeout=0,
            xonxoff=False, rtscts=False, dsrdtr=False, exclusive=True,
        )
        try:
            port.dtr = False
            port.port = port_info.device
            port.open()
            wait_bounded(DTR_SETTLE_SECONDS, deadline, clock, pause)
            # This is session initialization, before any challenge bytes are sent.
            port.reset_input_buffer()
            port.reset_output_buffer()
            port.dtr = True
            wait_bounded(DTR_SETTLE_SECONDS, deadline, clock, pause)
            for size in CHALLENGE_SIZES:
                exchange(port, make_challenge(size), deadline, clock, pause)
                verified += size
                report("PASS round=%d bytes=%d" % (round_number, size))
            port.dtr = False
            wait_bounded(DTR_SETTLE_SECONDS, deadline, clock, pause)
        finally:
            port.close()
    return {"rounds": rounds, "bytes": verified, "seconds": clock() - started}


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def positive_seconds(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return number


def write_once(fd, data):
    """A single POSIX write; backpressure returns control to the reader/deadline."""
    try:
        return os.write(fd, data)
    except OSError as error:
        if error.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
            return 0
        raise


def load_pyserial():
    try:
        import serial
        from serial.tools.list_ports import comports
    except ImportError as error:
        raise ProbeError("pyserial is required; run with the ESP-IDF Python environment") from error

    class NonblockingSerial(serial.Serial):
        def write(self, data):
            # Pyserial's POSIX Serial opens O_NONBLOCK. Its 3.5 write(timeout=0)
            # can nevertheless spin internally on EAGAIN, preventing our read
            # loop and deadline from progressing. Use one syscall per attempt.
            return write_once(self.fileno(), data)

    return NonblockingSerial, comports


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list matching ports without opening them")
    parser.add_argument("--port", help="explicit native CDC port; its USB identity is still verified")
    parser.add_argument("--rounds", type=positive_int, default=2, help="close/reopen rounds (default: 2)")
    parser.add_argument("--timeout", type=positive_seconds, default=30.0,
                        help="total deadline for all rounds, in seconds (default: 30)")
    args = parser.parse_args(argv)
    try:
        serial_factory, comports = load_pyserial()
        ports = list(comports())
        if args.list:
            candidates = [port for port in ports if matches_probe(port)]
            for port in candidates:
                print("%s %04x:%04x product=%r serial=%r"
                      % (port.device, port.vid, port.pid, port.product, port.serial_number))
            if not candidates:
                print("No matching native USB CDC probe ports.")
            return 0
        selected = select_probe(ports, args.port)
        print("Checking %s (%s); UART ports are excluded." % (selected.device, PRODUCT))
        result = run_probe(selected, serial_factory, args.rounds, args.timeout)
        print("PASS: %d bytes verified across %d reopen rounds in %.2f s."
              % (result["bytes"], result["rounds"], result["seconds"]))
        print("This verifies CDC bulk echo only; it does not validate UAC audio or electrical safety.")
        return 0
    except (ProbeError, OSError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
