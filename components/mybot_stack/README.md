# MyBot runtime bundle

This directory groups the three build inputs that make up the MyBot runtime:

- `mybot_sdk/` contains the read-only MyBot SDK snapshot and its ESP-IDF wrapper.
- `aosl/` contains the pinned AOSL component, its S31 wrapper configuration, and the
  source-built library used by MyBot and RTSA.
- `agora_rtc/` contains the separately licensed Agora RTSA component.

The directories remain independent ESP-IDF components named `mybot_sdk`, `aosl`, and `agora_rtc`.
The top-level `CMakeLists.txt` registers each through `EXTRA_COMPONENT_DIRS`; this bundle directory
is not itself a component. ESP-IDF does not automatically discover nested components here.

Platform code uses only the public SDK headers under `mybot_sdk/mybot/include/mybot`.
The SDK snapshot under `mybot_sdk/mybot` is read-only; ESP-IDF build integration belongs in
the wrapper `mybot_sdk/CMakeLists.txt`. AOSL keeps its upstream sources unchanged and applies
S31 configuration through `aosl/CMakeLists.txt` and `aosl/s31_port/`. The pinned RTSA header,
library and package metadata live directly under `agora_rtc/`.

The common location does not merge source trees or licenses. Update
[the dependency baselines](../../VENDORED_SOURCES.md) and
[third-party notices](../../THIRD_PARTY_NOTICES.md) whenever a bundled revision changes.
