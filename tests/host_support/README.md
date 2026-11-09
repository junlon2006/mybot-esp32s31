# Shared host mocks

`include/` contains declaration-only mocks shared by multiple host tests:

- basic ESP-IDF error codes and attributes;
- AOSL memory and time declarations;
- the common AOSL log declarations used by RTC/control tests.

Test-specific logging, allocator behavior, hardware registers, callbacks and fault
injection stay under each test's local `mocks/` directory. Extend this directory
only when every consumer needs the same stable declaration.
