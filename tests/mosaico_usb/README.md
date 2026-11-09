<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mosaico USB initialization and descriptors

Run `sh tests/mosaico_usb/run_init.sh` for USB initialization fault injection.
After resolving the pinned managed dependencies, run
`sh tests/mosaico_usb/run_descriptors.sh` to parse the production FS/HS descriptors.

The descriptor test checks factory-derived identity, UAC/CDC interface and endpoint
layout, 500 mA bus-power declaration, and fixed 48 kHz stereo S16 transport. It
requires the speaker's mute/volume feature unit and the microphone's direct
input-to-output path without a feature unit. Every entity reference, AudioControl
total length, configuration length and TinyUSB function length is checked; the
audio parser must end exactly at the CDC function boundary. The shared configuration
also retains the validated PIO/FIFO path and high-speed feedback interval.

These host tests establish descriptor and initialization contracts. USB host
enumeration, routing, current draw and audio quality require hardware results.
