<!-- SPDX-License-Identifier: Apache-2.0 -->
# Translation state projection tests

Run `sh tests/translation_state/run.sh`. This compiles the actual state model against
a minimal atomic-storage mock. It checks all ten device states, including values 8/9,
their public projections, loss/restoration of connectivity without discarding device
state, startup/shutdown/failure priority, transitions between high and low enum values,
and repeated reset/start cycles. No private bit-mask implementation is copied into
the test, so a regression to a three-bit device-state mask fails the public assertions.
