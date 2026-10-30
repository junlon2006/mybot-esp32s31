# PCM playback buffer lifecycle tests

Run `sh tests/pcm_playback_buffer/run.sh`. This compiles the actual shared PCM
buffer with a pthread host runtime and UBSan. It covers PSRAM allocation,
initialization failure, partial cleanup, timeout, worker ownership and restart.
The runtime/mocks also support `tests/korvo_s31_audio`; no camera path is included.
