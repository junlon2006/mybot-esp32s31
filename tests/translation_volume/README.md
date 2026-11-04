<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation volume host checks

Run `sh tests/translation_volume/run.sh` (UBSan by default). The harness executes
the production application key handler and translation-view refresh, media/audio
volume code, state projection and presenter. Only platform volume operations and
the LCD sink are mocked; common AOSL declarations come from `translation_control`.

Checks cover initial/restored hardware values distinct from software gain, actual
key adjustments, saturation at 0/100, hardware read/write failures, invalid values,
software fallback with no device control or failed device initialization, and the
volume delivered to the LCD. An unavailable read retries once per ten 100 ms control
ticks and restores the UI after recovery; healthy values do not poll the hardware.
These are host control/data-flow checks, not physical
loudness, codec calibration or real NVS persistence results.
