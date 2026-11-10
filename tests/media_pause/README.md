<!-- SPDX-License-Identifier: Apache-2.0 -->
# Local translation media pause

Run `tests/media_pause/run.sh`. This suite builds the production media pipeline,
audio volume logic and SPSC PCM ring buffer with mocked worker queues, transport,
announcements and capture/playback devices. It uses UBSan with cloud AEC enabled and disabled.

The tests cover the unchanged 16 kHz mono s16 and 60 ms cloud-AEC MIC/reference
contract, silent paused uplink cadence, remote/pending/device playback discard,
resume fences, repeated toggling, queue failures and delayed callbacks, timeout
recovery, independent capture, stop/session boundaries, and unchanged volume.
These host results establish control/buffer behavior, not TDM slots, sound quality,
physical silence timing or cloud translation results.

The independent USB pipeline checks that capture/playback contexts and failed-worker
ownership never affect the physical codec. It has no codec-volume, announcement or
reference-ring allocation; USB playback uses unity gain and digital capture sends
zero cloud-AEC reference. Pause discards USB playback, USB detachment notifies control,
and a playback write interrupted by intentional pause does not end the session.

Meeting teardown also exercises RTC disconnect before quiesce, a failed playback
stop retaining PCM ownership, then successful quiesce and restoration of idle playback.

Physical playback faults notify control and require a successful device stop/start
before another session. Fault injection covers retained errors, failed recovery,
successful later playback, and writes interrupted by intentional pause or stop.

Capture DSP carry is reset synchronously on the capture worker during session
flush and pause/resume fences. The mock backend retains old cleaned samples until
reset; tests require those samples to be discarded before uplink resumes, including
the path used by direction changes and reconnect. Reset failures latch capture_fault,
notify control, and reject readiness/resume. An already-faulted capture skips reset
so session cleanup can recover through stop/start without a reset retry loop or
an extra reset after fresh DSP initialization.
