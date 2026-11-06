# Mosaico board power and revision tests

Run `sh tests/mosaico_hardware/run.sh`. The real V1.2 hardware initializer is compiled
with UBSan and checked GPIO/eFuse/LEDC/I2C mocks. Cases cover incompatible and
unknown revisions before pin access, retained boot-stage rail levels, the official
100 kHz / 8-bit / 50 ms active-low ramp and following 10 ms settle, fade-service ownership, failures and retry,
and avoiding a second ramp after I2C initialization fails.

These tests check the sequence and failure ownership, not physical voltage,
inrush current, rail timing or electrical compatibility of an unknown board.
