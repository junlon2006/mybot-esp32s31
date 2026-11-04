<!-- SPDX-License-Identifier: Apache-2.0 -->
# Local translation media pause

Run `tests/media_pause/run.sh`. This suite builds the production media pipeline,
audio volume logic and SPSC PCM ring buffer with mocked worker queues, transport,
announcements and capture/playback devices. It uses UBSan only.

The tests cover the unchanged 16 kHz mono s16 and 60 ms cloud-AEC MIC/reference
contract, silent paused uplink cadence, remote/pending/device playback discard,
resume fences, repeated toggling, queue failures and delayed callbacks, timeout
recovery, independent capture, stop/session boundaries, and unchanged volume.
These host results establish control/buffer behavior, not TDM slots, sound quality,
physical silence timing or cloud translation results.
