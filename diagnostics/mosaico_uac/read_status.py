#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read Mosaico UAC diagnostic counters through a USB vendor IN request."""

import argparse
import ctypes
import ctypes.util
import json
import sys


def read_status():
    library = ctypes.util.find_library("usb-1.0")
    if not library:
        raise RuntimeError("libusb-1.0 was not found; on macOS install it with: brew install libusb")
    usb = ctypes.CDLL(library)
    pointer = ctypes.c_void_p
    byte = ctypes.c_ubyte
    usb.libusb_init.argtypes = [ctypes.POINTER(pointer)]
    usb.libusb_init.restype = ctypes.c_int
    usb.libusb_exit.argtypes = [pointer]
    usb.libusb_exit.restype = None
    usb.libusb_open_device_with_vid_pid.argtypes = [pointer, ctypes.c_uint16, ctypes.c_uint16]
    usb.libusb_open_device_with_vid_pid.restype = pointer
    usb.libusb_close.argtypes = [pointer]
    usb.libusb_close.restype = None
    usb.libusb_control_transfer.argtypes = [
        pointer, byte, byte, ctypes.c_uint16, ctypes.c_uint16,
        ctypes.POINTER(byte), ctypes.c_uint16, ctypes.c_uint,
    ]
    usb.libusb_control_transfer.restype = ctypes.c_int
    usb.libusb_error_name.argtypes = [ctypes.c_int]
    usb.libusb_error_name.restype = ctypes.c_char_p

    def check(result):
        if result < 0:
            name = usb.libusb_error_name(result).decode("ascii", errors="replace")
            raise RuntimeError("USB status read failed: " + name)
        return result

    context = pointer()
    check(usb.libusb_init(ctypes.byref(context)))
    handle = None
    try:
        handle = usb.libusb_open_device_with_vid_pid(context, 0x303A, 0x8000)
        if not handle:
            raise RuntimeError("Cannot open Mosaico UAC (303a:8000); check its connection")
        data = (byte * 1024)()
        # Device-recipient vendor IN, status protocol v1. This never claims an
        # audio interface, detaches its driver, resets USB or changes its config.
        length = check(usb.libusb_control_transfer(
            handle, 0xC0, 0x5A, 1, 0, data, len(data), 1000))
        status = json.loads(bytes(data[:length]).decode("utf-8"))
        if not isinstance(status, dict) or status.get("version") != 1:
            raise RuntimeError("Unexpected USB diagnostic status version")
        return status
    finally:
        if handle:
            usb.libusb_close(handle)
        usb.libusb_exit(context)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    print(json.dumps(read_status(), ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
