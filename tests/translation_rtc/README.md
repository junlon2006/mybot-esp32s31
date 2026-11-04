<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation RTC tests

Run `sh tests/translation_rtc/run.sh`. The test compiles and includes the production
`mybot_agora_rtc.c` against the bundled RTSA public header. Only vendor calls, AOSL queues,
memory, timers and clocks are replaced; callback dispatch, payload copying, route filtering and
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

Face-to-face cases verify two RTC identities with distinct tokens, one RTM login and one
shared-channel subscription, and accepting channel captions from both Agents. Aggregate
readiness waits for both RTC joins and each enabled TTS publisher, in either callback order;
a text-only listen route does not wait for a publisher. Per-direction sends and downlink
filtering use the corresponding connection. Any required route failure disables all audio.
A 15-second readiness deadline also covers reconnect without extending the deadline when a
second route reconnects. Missing joins/publishers, partial second-connection creation/join,
timer allocation failure, shared RTM login/subscribe rejection or timeout, invalid
cross-route contracts, and full teardown/restart are
injected independently. The mock timer is advanced explicitly; it does not measure hardware
scheduler latency or validate the server's two-Agent channel delivery.

RTM scenarios initialize and join through the public API before invoking the production
vendor callback entry points. They verify direct and subscribed UTF-8 JSON carried as either
STRING or BINARY, exact length/byte copying without requiring a terminating NUL, copied
sender/channel/custom metadata, binary bytes containing embedded NUL, unknown carrier-type
rejection, wrong channels both at capture and dispatch, stale login/subscription generations
after rejoin, and callback admission after shutdown. Binary payload validation and the
Agent/session authorization policy belong to the application tests; the wrapper preserves
bytes. Work accepted before the forwarding worker's finalization barrier may finish before
that barrier; newly arriving callbacks are rejected once callback admission closes.

This validates wrapper behavior, not the vendor binary or physical audio.
