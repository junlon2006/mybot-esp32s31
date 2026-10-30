# Bundled ESP32-S31 RTSA

The `agora_rtc` component links the user-approved ESP32-S31 RTSA v1.10.1,
build 1366494, directly from `include/` and `lib/riscv32/`. The header, library, package identity and
configuration are versioned; no download, import step or CI secret is required.
The original `.tgz` and generated firmware remain ignored.

- Original archive SHA-256: `7c6a81ab91f3b44c0cd581f84f779d7a022dac159020c19bdddd07c95da31dea`
- Library SHA-256: `573a00d58ad19cc0b66fc1e41f17e23ada34b77fe5c7a8c3e8fa8729f73fb6c5`
- Public header SHA-256: `29b1c21cbe416ee703eaa197ce623469619b1ed68cd3e9c66cb3abe2fafec7db`

Keep the supplied files byte-identical. The archive has 126 RISC-V RTSA members
and 49 unused x86-64 AOSL members. Only the local source-built `aosl` component
uses WHOLE_ARCHIVE; RTSA must remain a normal archive. The shared build checker
verifies target ABI, the selected archive members and local AOSL ownership.
Separate Agora distribution terms continue to apply; see THIRD_PARTY_NOTICES.md.
