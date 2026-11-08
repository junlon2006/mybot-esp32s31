# Mosaico input and board lifecycle host tests

Run `sh tests/mosaico_input/run.sh`. The board drivers provide only GPIO
sampling and hardware preparation; the shared router owns debounce, touch epochs,
UI command mailboxes, maintenance holds and SDK attachment. Fault injection covers
initialization, partial cleanup and retry, invalid samples, physical PLAY start/stop,
volume and provisioning keys, callback/destroy ordering and repeated SDK attach.

Touch samples alone never start or stop translation. A valid LVGL button click
posts its semantic event through a bounded mailbox. Tests cover language/volume
buttons, active-session language locking, cancel while connecting, cleanup retry,
unknown-outcome start suppression, stale SDK epochs, read-error cancellation,
held touches, burst coalescing and callback ordering. Mosaico's AI key has no short-press
action: three seconds requests provisioning and ten seconds requests factory reset.
SDK work runs on the key worker, outside the LVGL lock.

Parameterized language/volume/pause commands use a separate copied single-slot
mailbox with the same gesture/epoch validation. Tests verify copying, saturation,
state changes, detach, invalid values and read-error cancellation. A completed
start may cross the gesture-to-dispatch boundary only for a STOP command, so a
late start result cannot silently discard the user's cancel action.

Board tests preserve initialization ordering and defer Wi-Fi provisioning to the
application lifecycle. These host checks do not establish physical ADC voltages,
CST9220 coordinates or real-device timing.
