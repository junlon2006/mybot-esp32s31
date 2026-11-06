# Translation lifecycle fault tests

Run `./tests/translation_lifecycle/run.sh` from any directory. The runner compiles the real SDK
lifecycle implementation with a deterministic KV store, HTTP completion queue, allocator and UTC
clock, enables UBSan, and enforces a 30-second execution deadline.

The 36 scenarios exercise cancellation before dispatch and after HTTP start, late start results
across disconnect/reconnect and shutdown, durable unknown outcomes, partial starts carrying cleanup
IDs, exact original server/device/credential cleanup after endpoint changes or re-pair requests,
1/2/4/8/16/30-second stop retry intervals, revoked credentials, expiry without renewal, stale and
duplicate callbacks, repeated start/stop, queue rejection, allocation/KV/read/erase failures,
corrupt journals, shutdown ownership and local language persistence. Assertions check that cloud
IDs are saved before media callbacks and are retained until the client confirms `ended`; the client
HTTP/JSON contract has separate tests under `tests/translation_client`.

Stop completion assertions also inspect the error and pending flags visible to the real state
callback: confirmed cleanup clears temporary cleanup status, including after retry/auth rejection,
while the original start failure or latched storage failure remains available to the UI.

This test controls callback ordering and atomic mailboxes rather than creating real RTC/network
threads. It does not establish network behavior, acoustic echo cancellation, speaker timing, audio
quality, or long-duration hardware stability.

Mode selection is volatile and starts at listen after reboot. Mode/language changes are
rejected as soon as a start is requested, during an active session or while cleanup remains.
Face-to-face failures retain one parent ID and retry its cleanup; they never create a second
parent or persist a second journal.

Journal coverage includes recovery from version 1 fixed records and version 2 compact records,
unknown and known sessions, malformed/truncated/oversized fields, unsupported versions and trailing
bytes. Rejected records remain stored and block another cloud start. A constrained KV model reserves
67 entries for the journal and charges a replacement before reclaiming the previous blob, including
one extra chunk header for the page split seen on hardware. It reproduces the old 1456-byte record's
49-entry first write and failed replacement, then verifies compact start/stop cycles with a 511-byte
synthetic credential. This is an entry-budget model, not an emulator of the NVS filesystem.

Meeting selection tests commit LISTEN_SPEAK mode, USB source and languages together,
keep the previous selection on language persistence failure, reject changes once
a start is requested, and ensure meeting creates two routes under the existing single parent.
