<!-- SPDX-License-Identifier: Apache-2.0 -->
# HTTPS body trace host tests

Run `sh tests/conversation_http_trace/run.sh` from the repository root.

The test uses synthetic requests/responses and mock PSRAM allocation/logging, with
UndefinedBehaviorSanitizer. Set `SANITIZERS=address,undefined` to additionally run
AddressSanitizer on hosts that support its runtime. It checks successful short writes,
arbitrary byte fragmentation, a complete 12 KB UTF-8 body, exact request/response
capture boundaries, chunk extensions/trailers,
HTTP errors, early EOF, connection-close framing, allocation failure, capacity
limits, GET binding-status and POST start/stop, empty request/response bodies,
HEAD/204/304 responses, chunked requests, accepted wire-byte counts, request path
redaction, query/header exclusion and interleaved connection contexts. Each
request/response must have exactly one body log record, including empty bodies;
text bytes are compared in full and require no extra logging allocation. Bodies
containing NUL bytes produce a single complete hex record, checked with a 600-byte
fixture. A failed temporary hex allocation must emit an explicit unavailable
record, retain the HTTP result and release all captured buffers. Capture overflow
still emits the entire captured body once with `complete=no reason=capacity`.
The suite makes no network requests and
does not establish real-device HTTPS behavior.
