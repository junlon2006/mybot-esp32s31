<!-- SPDX-License-Identifier: MIT -->
# Wi-Fi lifecycle and provisioning identity regressions

Run `sh tests/wifi_manager/run.sh`. The suite builds the production
`wifi_manager.cc`, platform `wifi.cpp` and their real headers. Only the ESP/storage
boundary and station/AP driver methods are faked; the manager synchronization,
event dispatch and platform provisioning flow are exercised unchanged.

The fake station stop models IDF event-handler unregister: it waits for an event
loop mutex while an in-flight callback owns that mutex. A gate delivers the
callback after stop has entered, so station shutdown and station-to-AP mode
switches deterministically reproduce the former lock inversion. Callbacks also
query manager state and replace the event callback. Additional cases cover AP
shutdown, AP-to-station switches, concurrent lifecycle operations and a competing
start while a mode-transition notification is delayed. A gated station start also
checks that a concurrent power-save setting waits for driver startup to complete.

Provisioning cases use synthetic STA MACs to check the `mybot-xxxx` identity from
platform initialization through manager configuration, advertised AP name and the
UI getter. Expected CRC-16/CCITT-FALSE values come from the independent Python
`binascii.crc_hqx(mac, 0xffff)` oracle. Cases cover four devices sharing a MAC prefix,
middle-byte differences, leading zeroes, lowercase hex, repeat provisioning,
insufficient output capacity and MAC-read failure/retry. Each of the six MAC bytes
is checked against a fixed mutation vector. With the first four bytes fixed, all
65,536 trailing-byte combinations produce different short IDs; the 16-bit space
cannot guarantee uniqueness across all 48-bit MACs.

Every case runs in its own child process, with a three-second parent watchdog.
A deadlock fails the suite without hanging the runner. `WIFI_MANAGER_SOURCE` and
`WIFI_MANAGER_INCLUDE_DIR` can select an earlier real manager source/header pair
to check the regression. `PLATFORM_WIFI_SOURCE` can select an earlier platform
source; comparisons predating the CRC helper must also supply its current
definition to link the CRC-only cases. All paths default to production sources;
no mock manager header is provided.

These host tests establish synchronization and AP identity generation/publication
behavior. IDF driver shutdown latency and radio behavior require hardware results.


The suite also exercises full-width STA fields and NVS profile migration, write/commit
failure and retry. The platform provisioning case delivers the completion callback
from an HTTP thread, then lets the control owner switch the real manager to station mode.
After IDF resolves cJSON, `sh tests/wifi_manager/run_portal_json.sh` tests both list
encoders and the production HTTP submission handler with fragmented bodies, failed
persistence, and a client disconnect during the success response.
