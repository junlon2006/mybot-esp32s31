<!-- SPDX-License-Identifier: Apache-2.0 -->
# Persistent storage startup

Run `sh tests/storage_init/run.sh`. The real startup helper is compiled with
UBSan against NVS mocks. Success, no-free-pages, newer-format, allocation and
generic errors each perform one initialization attempt and never erase data.
Failure logs identify the error without exposing stored values.

The host check verifies the startup decision; it does not emulate flash or NVS
page recovery. A device with unreadable persistent storage requires explicit
owner recovery after preserving its credentials and pending session identity.
