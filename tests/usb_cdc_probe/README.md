<!-- SPDX-License-Identifier: Apache-2.0 -->
# Native USB CDC probe host tests

```sh
sh tests/usb_cdc_probe/run.sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/usb_cdc_probe -p 'test_*.py' -v
```

The C suite includes production `main/usb_cdc_probe.c` with mocked TinyUSB and
FreeRTOS boundaries. It exercises bounded partial reads/writes, backpressure,
generation resets, DTR, suspend/resume, callback/worker ownership and startup
failure cleanup. In particular, the USB task must stop before freeing CDC
callback state; failed teardown must retain ownership and reject another start.
The runner uses UBSan and compiler format checking.

The device-event hook is exercised in ISR and task contexts, including foreign
ports, unknown events and counter wrap. Tests ensure ISR handling only updates
bounded counters, and hardware/status logging stays in ordinary task context.
The real S31 controller snapshot is compiled in the target firmware and reviewed
for read-only accesses; host tests stub it and do not emulate hardware registers.

The Python suite uses fake serial devices and a fake clock to verify strict USB
identity selection, UART rejection, partial I/O, deadlines, reconnects, corrupted
or extra bytes and a nonblocking write when the OS reports backpressure. It
requires only the standard library and never opens a physical serial port.

These checks do not emulate USB descriptors, endpoint DMA or electrical wiring.
The target build checks the real pinned TinyUSB APIs; native enumeration and
end-to-end echo require a flashed board and an appropriate connection. A passing
CDC bulk test does not validate UAC isochronous audio or VBUS safety.
