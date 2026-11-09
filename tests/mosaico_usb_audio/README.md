<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mosaico USB audio host tests

Run `sh tests/mosaico_usb_audio/run.sh`. UBSan is enabled by default and stops on
the first finding; CI explicitly uses the same flags. `TEST_SANITIZE_FLAGS` can
select another supported sanitizer for a separate host run. The real project USB
adapter and filters run against mocked TinyUSB, DCD and locks.

Tests cover static preparation without package runtime dependencies, endpoint accounting and
unchanged non-audio forwarding; fixed 48 kHz clock controls, channel mute/volume readback,
short/full/oversized RANGE probes and rejection of invalid or absent microphone
Feature Unit control requests; owned OUT queues,
bounded capture, channel mute/gain, alternate-interface
changes and concurrent stop/close/destroy; host-paced USB IN packets, independent
playback lifecycle, short-utterance prebuffering, overflow/underflow and closed-host
sample discard. Interpolation and decimation checks verify numerical passband/image
rejection and identical results for varied chunk boundaries. Clock simulations run
both +417 ppm and -417 ppm offsets for 144 seconds with bounded queues and no new
underflow/drop after priming.

The IN wrapper must intercept `usbd_edpt_xfer_fifo`, above the weak DCD fallback
inside `usbd.c`, and call `usbd_edpt_xfer` for normal endpoint busy bookkeeping.
Delivery checks require its live final-ELF symbol and linker option; final firmware
inspection also checks that the audio class calls the wrapper.

These checks do not establish Mac enumeration, actual USB interrupt timing, physical
MIC/speaker quality, cloud translation, AEC performance or endurance. No codec, I2S,
RTC, PHY or real TinyUSB task is provided by the host mocks. USB descriptor checks
against pinned headers run separately in `tests/mosaico_usb/run_descriptors.sh`.

UBSan checks undefined behavior; the threaded cases check specific ownership and
ordering scenarios. Neither establishes that USB ISR/task interaction is free of
data races. No successful ThreadSanitizer result is recorded. An optional macOS
ASan run of the control suite failed in libpthread/dyld initialization before main,
so that run produced no application test result.
