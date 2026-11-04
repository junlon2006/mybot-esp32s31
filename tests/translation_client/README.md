<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation protocol tests

Run `sh tests/translation_client/run.sh`. The host executable compiles the production device
client and the SDK's actual namespaced JSON parser. HTTP and wall time are controlled locally;
no network request is made. The default sanitizer is UBSan; use `SANITIZERS=address,undefined`
for allocator and memory checks.

Coverage includes the audio/AEC/QoS request, input/output language mapping, route-only media
credentials, normalized listen/speak routes, four independent identities, same-project/channel
validation, caption-only listen and required speak TTS, UTC token expiry,
unsynchronized-clock rejection before HTTP, safe cleanup after a clock reset during HTTP,
and a complete synthetic service response for Chinese-to-English translation. It also checks
the 30-second start deadline, the default deadline for stop, partial start cleanup, ambiguous
transport/start results, invalid input, local allocation failure, and stop responses that have
not confirmed `ended`. These are protocol tests, not real-device audio or Global service
validation.

Face-to-face requests treat the selected languages as local/remote, while media requests
treat them as input/output. Both response orders, missing/duplicate directions, partial-start
cleanup and every JSON allocation failure are covered for the two-route response.
