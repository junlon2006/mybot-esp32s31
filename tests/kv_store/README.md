<!-- SPDX-License-Identifier: Apache-2.0 -->
# Persistent store diagnostics

Run `sh tests/kv_store/run.sh`. The host test injects failures in open, blob-size
lookup, blob read/write, commit, erase and statistics queries. It checks unchanged
return values, missing-key handling, initialization despite unavailable statistics,
commit ordering, and absence of retries or erase calls after write failure.

The logging mock checks printf arguments at compile time and verifies that
serialized record contents never appear in logs. These tests do not establish
actual device NVS capacity, flash health or heap availability.
