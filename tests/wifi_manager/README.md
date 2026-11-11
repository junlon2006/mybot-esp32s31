<!-- SPDX-License-Identifier: MIT -->
# Wi-Fi manager concurrency regression

Run `sh tests/wifi_manager/run.sh`. The suite builds the production
`wifi_manager.cc` and its real manager/station/AP headers. Only the ESP boundary
and station/AP driver methods are faked; the manager synchronization and event
dispatch are exercised unchanged.

The fake station stop models IDF event-handler unregister: it waits for an event
loop mutex while an in-flight callback owns that mutex. A gate delivers the
callback after stop has entered, so station shutdown and station-to-AP mode
switches deterministically reproduce the former lock inversion. Callbacks also
query manager state and replace the event callback. Additional cases cover AP
shutdown, AP-to-station switches, concurrent lifecycle operations and a competing
start while a mode-transition notification is delayed. A gated station start also
checks that a concurrent power-save setting waits for driver startup to complete.

Every case runs in its own child process, with a three-second parent watchdog.
A deadlock fails the suite without hanging the runner. `WIFI_MANAGER_SOURCE` and
`WIFI_MANAGER_INCLUDE_DIR` can select an earlier real manager source/header pair
to check the regression. Both default to the production component; no mock
manager header is provided.

These host tests establish synchronization behavior, not IDF driver shutdown
latency, radio behavior or physical hardware results.
