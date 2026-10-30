# Conversation HTTP body trace host tests

Run `sh tests/conversation_http_trace/run.sh` from the repository root.

The test uses synthetic requests/responses and mock PSRAM allocation/logging, with
UndefinedBehaviorSanitizer. Set `SANITIZERS=address,undefined` to additionally run
AddressSanitizer on hosts that support its runtime. It checks successful short writes,
arbitrary byte fragmentation, large and UTF-8 bodies, chunk extensions/trailers,
HTTP errors, early EOF, connection-close framing, allocation failure, capacity
limits, unrelated endpoints, interleaved connection contexts and complete hex
output for bodies containing NUL bytes. It makes no
network requests and does not establish real-device HTTPS behavior.
