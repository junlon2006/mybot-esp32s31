# Korvo S31 display host tests

Run `sh tests/korvo_s31_display/run.sh` or prepend
`TEST_SANITIZE_FLAGS='-fsanitize=undefined -fno-sanitize-recover=all'`.

The tests compile the production RGB panel, GT1151 wrapper and LVGL adapter.
Constructor mocks verify the SUB3 pins/timing, RGB565 byte order, two PSRAM
framebuffers, shared board-owned I2C and touch polling. Fault injection covers
allocation and initialization failures, partially returned handles, frame timeout,
callback completion before waiting and during deletion, failed cleanup/retry,
reference counting, and repeated initialization/destruction. The mock rejects
framebuffer/display/semaphore release while RGB still owns them.

Touch polling checks forward pressed, released and invalid samples to the key
bridge, including I2C read/get failures and cancellation during display teardown.
Conversation delivery and SDK lifetime checks run in `tests/korvo_s31_input`.

These checks do not establish physical LCD colors, PSRAM bandwidth, touch
orientation or actual GDMA timing. Those require the Korvo-1 V1.1 and SUB3.

Snapshot delivery checks exercise capability-only and audio-source-only changes through
the real adapter and its refresh timer, including queued value ownership and clearing
back to the default codec source. This catches fields lost between presenter and view.
