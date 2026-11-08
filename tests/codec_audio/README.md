# Mosaico codec audio host checks

Run `sh tests/codec_audio/run.sh` to compile the actual platform driver with UBSan,
ESP Codec Device public headers, and the existing pthread/PSRAM host runtime.
The test replaces hardware and the playback FIFO with fault-injection boundaries;
the playback FIFO itself is covered by `tests/pcm_playback_buffer/test_pcm.c`.

Coverage includes initialization and allocation failures, failure during start, partial
cleanup and retries, rejection of a new stream during incomplete cleanup, independent
capture/playback ownership, repeated starts/stops, codec-open reference lifetime,
blocked capture and bounded stop, playback stop failure, volume persistence, native
TDM configuration, PCM slot conversion and partial progress on timeout.

Public `destroy()` transfers ownership even when cleanup times out. The driver
retains the context in its deferred list and retries once during a subsequent
stream init or destroy. New streams are rejected while cleanup still fails.
Callers must discard the old pointer after destroy and must not call it twice.
Tests cover a caller dropping that pointer, a failed first init retry, successful
recovery on the next init, retained playback resources, mutex timeout, and a read
callback finishing before its deferred context is freed.

These are host results. They do not establish actual waveform, audio quality,
microphone gain, PA sequencing or concurrent hardware stability.

The suite verifies the Mosaico ES8311 backend: 16 kHz
stereo Philips/MCLK256 configuration, 0x19 codec address, RX slot-zero extraction,
mono duplication and bounded lifecycle/failure ownership. These host checks do not
establish physical clock, slot, loudness or AEC quality.
