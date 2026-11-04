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

The UI/control tests also require full RTC finalization at each session end and a
successful RTC cleanup preflight before the next cloud START. Failed finalization
keeps the UI error visible and rejects repeated cloud starts. RTC wrapper tests cover
vendor cleanup failure retention and switching App IDs after successful finalization.
The stop callback is also exercised after cleanup state publication: RTC failure
replaces temporary cleanup hints but preserves storage/start errors, and audio teardown
still runs when RTC finalization fails.
