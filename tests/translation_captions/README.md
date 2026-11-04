<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation caption model tests

Run `sh tests/translation_captions/run.sh`. The suite compiles the production
caption model with the real shared JSON parser and injects allocator failures.

Checks cover original/translated pairing, translation-first fallback, shorter
interim revisions, independent final flags and relative timestamps, duplicates,
UTF-8 boundaries and truncation, a bounded 64-record history, reset and invalid
JSON/UTF-8/IDs/times/depth. Every allocation failure retains the previous model
and releases temporary memory. Entries already evicted from the bounded ring
are not retained in an unbounded deduplication index.

Application routing, inbox ownership and nonblocking copied reads are exercised
by `tests/translation_control/test_captions.c`. RTM payload types and callback
generations are covered by `tests/translation_rtc`; rendered pages and font
coverage are covered by `tests/translation_ui`. These are host tests, not live
service or physical-display validation.
