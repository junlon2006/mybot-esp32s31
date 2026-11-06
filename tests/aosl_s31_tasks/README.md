<!-- SPDX-License-Identifier: Apache-2.0 -->
# S31 AOSL task stacks

Run `sh tests/aosl_s31_tasks/run.sh`. The host CMake case uses the production
component wrapper to compile the unchanged AOSL thread HAL with its scoped adapter.
It verifies exact project-worker-name allocation, unchanged stack size/priority, matching
deletion after normal return and explicit thread exit, renamed-task ownership,
misspelled/truncated-name rejection, and failed wrapper/task allocations.
Undefined-behavior sanitizer is enabled. PCM workers, `service_mpq` (HTTP/JSON)
and `mybot_rtc` (RTC forwarding) use PSRAM; `control_mpq` performs persistence
and keeps an internal stack, along with the opaque RTSA worker tasks.

Audited PSRAM workers exit through a static internal reaper. Its queue, task control
block and stack exist before the first worker starts; exit queues the worker and
parks it without allocating a temporary cleanup task. The reaper uses IDF's external
WithCaps deletion path, which waits for the target to stop on every CPU core.

These tests mock the FreeRTOS scheduler. Hardware must still verify concurrent
shutdown and real resource headroom. No firmware build is performed by this script.
