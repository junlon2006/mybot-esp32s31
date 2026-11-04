<!-- SPDX-License-Identifier: Apache-2.0 -->
# HTTP request deadline host tests

Run `tests/translation_http/run.sh`. The harness compiles the production HTTP client
and URL parser with a scripted TLS transport, fake monotonic time and UBSan. It
never sleeps or contacts a service, and reads the existing AOSL interface headers.

It verifies a complete response arriving after 5 seconds succeeds with the explicit
30-second budget, default GET/POST retain 5 seconds, and connection, short writes and
fragmented reads consume one shared budget. Truncated bodies, timeout after partial
progress and nonpositive timeouts must fail; all opened connections and response
allocations must be released. These tests do not establish ESP-TLS scheduling or
real-device translation behavior.
