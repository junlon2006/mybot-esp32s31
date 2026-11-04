# Translation UI host checks

Run `sh tests/translation_ui/run.sh`. Both display languages compile the real LVGL
view and the SDK's public language list against an object/event model. The checks
cover the native 800×480 layout bounds; initial widget allocation failure and
repeated cleanup; home, three scenes, and non-operational scene previews; direct
language selection and duplicate-language rejection; language submission rejection,
controls locked across delayed acknowledgements, timer expiry and unrelated volume
or unchanged old-error readback, subsequent edits retaining the acknowledged pair,
and failed-save recovery;
slider release and actual
volume readback; acknowledged pause/resume; fixed demo captions, full text and
history; start/cancel/late-start/stop/cleanup retry; disabled controls for unknown
outcomes; and pairing code/Wi-Fi provisioning SSID. Language changes use the public
nonblocking SDK mailbox directly and remain locked until their pair is read back
or a failure/state change arrives. Other commands retain a bounded local guard so
a dropped platform mailbox message permits retry.

These tests verify semantic callbacks and view ownership, not touch-panel
coordinates, physical LCD colors or real-device rendering. The S31 input and
display tests separately cover input cancellation and hardware lifecycle. Font
checks require all used Chinese glyphs at each of the 20, 28, and 40 pixel sizes.
