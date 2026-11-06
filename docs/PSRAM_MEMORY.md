# S31 memory policy

[English](PSRAM_MEMORY.md) · [简体中文](PSRAM_MEMORY.zh-CN.md)

The S31 board profile enables Octal PSRAM; target defaults select 320 MHz CPU, 200 MHz PSRAM, a 2,048-byte internal malloc preference threshold and 65,536-byte internal reserve. Actual memory use and long-duration stability must be measured on the physical board.

Platform audio data, PCM FIFO/staging, supported task stacks, prompts and HTTPS wrappers request PSRAM when their lifecycle permits. LVGL draw buffers use PSRAM. Mosaico keeps 40-row partial buffers and an internal DMA staging area; Korvo uses two external RGB framebuffers. NVS uses IDF's PSRAM-preferred cache allocation; TLS selects external allocation. Driver allocators still own capabilities/alignment/cache requirements.

Synchronization objects, task control blocks, ISR state and stacks that perform cache-disabled Flash/NVS operations remain internal where required. A reserve does not guarantee a single contiguous block. The ESP32-S31 supports selected DMA access to PSRAM; the earlier S3 rule that every DMA buffer must be internal does not apply. Respect each S31 peripheral driver's requirements and RGB cache/VSYNC synchronization.

PSRAM task stacks use IDF capability-aware task allocation. Owners wait for I/O/callback completion and worker suspension before deleting the task and freeing its stack. Timeout preserves ownership for later cleanup. The shared PCM tests cover failure, partial cleanup, timeout and repeated lifecycle operations.

Video and its S3 DMA-buffer workaround are absent. Do not tune memory based on removed camera code or copy S3 cache/heap reserves blindly. The [validation record](s31-validation.md) separates configuration/build checks from measured hardware results.


The S31 AOSL adapter places audited PCM capture/playback/send task stacks (`cap_mpq`,
`pb_mpq`, `mybot_mpq`), the HTTP/JSON worker (`service_mpq`), and the project RTC
forwarding worker (`mybot_rtc`) in PSRAM. Duplex media uses six 16 KiB audio stacks;
with the 16 KiB HTTP and 8 KiB forwarding stacks, this reduces internal-stack demand
by about 120 KiB without shrinking any stack or changing priority.

The NVS-writing control worker and opaque vendor RTSA tasks retain internal stacks.
Deletion selects the matching allocator from the actual stack address. Before the first
PSRAM AOSL worker, the adapter creates a permanent static internal reaper: a 16-handle
queue, TCB and aligned 2 KiB stack, about 2.5 KiB of internal memory in total. Worker
return/self-exit queues its handle and suspends; the reaper calls IDF's external
WithCaps deletion path, which waits until the worker is off every core. Full queues
apply backpressure. Exit needs no temporary task or heap allocation; reaper startup
failure rejects worker creation. The reaper remains available across AOSL restarts.
Allocation logs report placement and, on failure, the requested
size and free/largest internal and PSRAM blocks. Existing PCM queues, USB media objects,
captions, RTC event copies, TLS and codec scratch already use PSRAM. USB ISR-visible
rings/packets remain internal. This change retains static application layout and does
not enable the global external-BSS option, which also relocates Wi-Fi/lwIP state.
