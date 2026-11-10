<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation result-mailbox tests

Run `sh tests/translation_control/run.sh`. This host test includes production
`mybot_app.c`, preserving its HTTP worker, result ownership, notification callback,
mailbox drain and service cleanup code. HTTP and lifecycle boundaries are mocked so
the test can observe which requests are delivered and when they are freed.

Scenarios cover a normal delayed result, an unexecuted START cancelled before HTTP,
an executed START finishing after notifications are disabled, notification queue
failure, a notification processed after cleanup already drained/freed its result,
STOP versus polling behavior during shutdown, and a failed service-queue join in the
control finalizer followed by a successful outer join that still delivers the parent
ID before lifecycle finalization and storage teardown. Every scenario checks allocator
balance. Separate lifecycle and protocol tests validate the boundaries mocked here;
these tests do not establish vendor thread scheduling or real-device behavior.

Application shutdown also retains its AOSL reference while any physical/USB media,
HTTP queue or RTC teardown is incomplete. Retry tests cover failed worker joins,
retained media buffers, a standalone RTC failure, and stopping before media was
initialized. A new runtime must not overwrite resources left by an earlier run.

The UI/control tests also require full RTC finalization at each session end and a
successful RTC cleanup preflight before the next cloud START. Failed finalization
keeps the UI error visible and rejects repeated cloud starts. RTC wrapper tests cover
vendor cleanup failure retention and switching App IDs after successful finalization.
The stop callback is also exercised after cleanup state publication: RTC failure
replaces temporary cleanup hints but preserves storage/start errors, and audio teardown
still runs when RTC finalization fails.

Every scene must stop device playback before RTC finalization. Tests retain
separate device-owned PCM and check physical/USB quiesce ordering, independent
attempts when one output fails, successful end-session recovery and retained
recovery requirements when cleanup still fails.

The caption integration tests use the production model and JSON parser with the
application's real RTM handlers and copied-read API. They cover sender/channel
filtering, borrowed payload ownership, the eight-message inbox and overflow,
session generations, pause/end behavior, both nonblocking read gates, allocation
failures and shutdown occurring between admission and copying. No live RTM
service is used.

The face-to-face tests exercise production app routing with both cloud-AEC
configurations. They check one real microphone destination, shared playback
reference samples, silent unselected routes, initial closed input, media-mode
compatibility, selected-only playback, direction fences, stop races, bounded
producer-gate contention, failed pause/resume and scratch allocation failure.
Pipeline pause fencing itself is covered by `tests/media_pause`. UI command tests
also verify mode/direction completion revisions, including repeated failures,
successful retries and unrelated volume/caption refreshes.

Unknown-start recovery is a copied asynchronous command with an explicit affirmative value.
Tests verify consumption-time state checks and completion revisions on success or rejection,
including unchanged errors. Active RTC/USB routes block it, while background physical capture
and initialized idle USB resources do not. Real journal durability and storage failures are
covered by `tests/translation_lifecycle`.

Meeting selects USB capture/playback together with LISTEN_SPEAK and local/remote
languages. Control tests require both USB capabilities, reject the obsolete USB+LISTEN
topology, prevent cloud start when USB cannot start, and release prepared USB media
after a failed/cancelled start. Both cloud-AEC configurations exercise simultaneous
MIC→SPEAK and USB→LISTEN uplink with isolated remote destinations, meeting pause,
USB disconnect admission closure, and retained resources after worker/RTC teardown
failure. The USB pipeline is freed only after both RTC callbacks and workers drain.

Start-admission tests deterministically inject capture/playback faults after the
initial fault reads and before cancellation resets, using an optional atomic-read
hook enabled only in the UI fixture. Media, face-to-face and meeting must reject
the current cloud start, display MEDIA_NOT_READY and retain the recovery marker;
the next attempt must recover before it can request a session and join RTC.

RTC readiness is handed from the RTC callback to the control tick. Reconnect
tests verify that callbacks never wait for media workers or publish ready,
that both meeting pipelines drain before admission, and that newer connection
states, session changes, pending stops and cleanup failures invalidate recovery.
User pause survives the handoff. `tests/media_pause` checks the actual PCM queues
and capture/reference contents across these worker barriers.
