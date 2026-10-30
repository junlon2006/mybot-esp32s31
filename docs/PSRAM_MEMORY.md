# S31 memory policy

[English](PSRAM_MEMORY.md) · [简体中文](PSRAM_MEMORY.zh-CN.md)

The S31 board profile enables Octal PSRAM; target defaults select 320 MHz CPU, 200 MHz PSRAM, a 2,048-byte internal malloc preference threshold and 65,536-byte internal reserve. Actual memory use and long-duration stability must be measured on the physical board.

Platform audio data, PCM FIFO/staging, supported task stacks, prompts and HTTPS wrappers request PSRAM when their lifecycle permits. LVGL allocations and the two RGB framebuffers use external memory. NVS uses IDF's PSRAM-preferred cache allocation; TLS selects external allocation. Driver allocators still own capabilities/alignment/cache requirements.

Synchronization objects, task control blocks, ISR state and stacks that perform cache-disabled Flash/NVS operations remain internal where required. A reserve does not guarantee a single contiguous block. The ESP32-S31 supports selected DMA access to PSRAM; the earlier S3 rule that every DMA buffer must be internal does not apply. Respect each S31 peripheral driver's requirements and RGB cache/VSYNC synchronization.

PSRAM task stacks use IDF capability-aware task allocation. Owners wait for I/O/callback completion and worker suspension before deleting the task and freeing its stack. Timeout preserves ownership for later cleanup. The shared PCM tests cover failure, partial cleanup, timeout and repeated lifecycle operations.

Video and its S3 DMA-buffer workaround are absent. Do not tune memory based on removed camera code or copy S3 cache/heap reserves blindly. The [validation record](s31-validation.md) separates configuration/build checks from measured hardware results.
