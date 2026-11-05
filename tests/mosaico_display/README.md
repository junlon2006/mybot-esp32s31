# Mosaico display lifecycle tests

Run `sh tests/mosaico_display/run.sh`. For undefined-behavior checks, use
`TEST_SANITIZE_FLAGS=-fsanitize=undefined sh tests/mosaico_display/run.sh`.

The actual Mosaico LVGL adapter runs against host SPI/LVGL/touch mocks. Eighteen
scenarios cover partial initialization, reference ownership, normal flush,
byte-swapped DMA staging, four-pixel alignment, transfer timeout and late completion,
touch release/error gating, and retried cleanup. These tests do not establish the
physical panel's color/orientation, DMA timing, touch coordinates or visual quality.

The runner also compiles the production CST9220 adapter with a fake interrupt/packet source.
It checks idle polls without duplicate I2C reads, short IRQ pulses and level fallback, an IRQ
during level sampling, cached held contacts, explicit release, invalid reads, coordinate
bounds, cleanup retries and fresh-owner state. Controller protocol bytes are not emulated.
