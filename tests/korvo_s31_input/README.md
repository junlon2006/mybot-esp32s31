# Korvo S31 ADC keys and board host tests

Run `sh tests/korvo_s31_input/run.sh`. Add
`TEST_SANITIZE_FLAGS='-fsanitize=undefined -fno-sanitize-recover=all'` for UBSan.

The production ADC input runs on a host thread with deterministic 20 ms samples.
Checks cover ADC/worker initialization failures, partially returned handles,
failed ADC cleanup and retry, debounce, PLAY start/stop state gating, volume/PAIR,
ADC and calibration errors, SET held for three seconds while MyBot is stopped,
and repeated SDK attach/detach. A blocked callback proves detach waits before its
user data may be released. The board tests exercise prepare failures, display
cleanup, idempotent prepare, descriptor registration, automatic/manual provisioning
callbacks, network errors, and process-lifetime hardware ownership.

Touch tests execute the production mailbox on the same polling worker: tap to
start/stop, release-only delivery, long hold without repeat, bounded tap bursts,
read-error cancellation, readiness changes, discarded detached input, stale
gestures across SDK lifetimes, and repeated toggles. A blocked SDK callback also
proves the touch producer remains non-blocking while detach waits, so LVGL does
not acquire the callback mutex.

The official ADC calibration helper is not modified or simulated as a hardware
calibration result. Real voltage thresholds, noise and calibration accuracy still
require the Korvo-1 V1.1.
