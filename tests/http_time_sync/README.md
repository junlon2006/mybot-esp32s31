<!-- SPDX-License-Identifier: Apache-2.0 -->
# HTTPS Date time bootstrap host tests

Run `tests/http_time_sync/run.sh`. The default build enables UndefinedBehaviorSanitizer;
`SANITIZERS=address,undefined` also enables AddressSanitizer.

These tests feed only the response parser and replace the clock and `settimeofday`.
They cover every header split, bytewise feeds, independent connections, UTC calendar
and range validation, strict status/header syntax, conflicting/invalid Date fields,
header bounds, missing/incomplete headers, current-clock gating, clock-set failure,
TCP/IP dispatch failure and an SNTP update queued ahead of the bootstrap callback.
The parser has no allocations; its bounded state is embedded in the TLS connection's
existing PSRAM allocation. The tests do not establish TLS trust, server Date accuracy,
UDP NTP availability, or real device translation behavior.
