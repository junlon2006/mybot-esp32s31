<!-- SPDX-License-Identifier: Apache-2.0 -->
# USB Host descriptor probe host tests

Run `sh tests/usb_host_probe/run.sh`. The runner builds the production source with
strict compiler warnings and UBSan; `CC` and `SANITIZERS` can override those defaults.

The tests invoke the public start API, capture its FreeRTOS task, and supply a
bounded USB event loop. Each scenario runs in a fresh process so the one-attempt
startup guard is tested without resetting private production state.

Coverage includes task/install/client-registration failure, bounded startup
cleanup, descriptor retrieval errors, unplug/replug, reconnection queued during
close failure, retained ownership after persistent close failure, and event-loop
errors. The mocks reject device API calls from callbacks and access to a closed
device. They do not provide filesystem, MTP, ADB, interface-claim or transfer APIs.

These checks establish application control flow and ownership only. They do not
validate physical USB enumeration, phone compatibility, cable wiring, VBUS power,
controller interrupts, or real RTOS timing.
