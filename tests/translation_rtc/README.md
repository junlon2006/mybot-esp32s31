<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation RTC tests

Run `sh tests/translation_rtc/run.sh`. The test compiles and includes the production
`mybot_agora_rtc.c` against the bundled RTSA public header. Only vendor calls, AOSL queues,
memory and clocks are replaced; callback dispatch, payload copying, route filtering and
init/join/leave/fini code execute unchanged. The queue mock defers asynchronous callbacks
and drains earlier work before synchronous commands to model the forwarding worker's FIFO
ordering and finalization barrier.

Scenarios verify disabled automatic subscriptions, unchanged G.722/16 kHz/mono/60 ms cloud
AEC and QoS options, string-account mapping before unmuting, rejection of unrelated audio,
publisher lookup, Agent/publisher loss, stale connections, callback shutdown and subscription
failure. Public API scenarios cover switching App IDs after per-session finalization,
whole-service recovery after connection-destroy failures during join/leave/fini, terminal
vendor-finalization failure, retrying forwarding-queue destruction without repeating vendor
finalization, and callbacks delivered during teardown before connection IDs are reused by
the next session. Successful vendor finalization must stop vendor callbacks; the mock does
not establish that behavior in the supplied binary.

This validates wrapper behavior, not the vendor binary or physical audio.
