# S31 audio playback

[English](AUDIO_PLAYBACK.md) · [简体中文](AUDIO_PLAYBACK.zh-CN.md)

The public MyBot audio contract is 16 kHz mono signed 16-bit PCM with frame counts. Mosaico maps that PCM to ES8311 standard Philips I2S with two 16-bit slots and MCLK256; Korvo maps it to ES8389 four-slot 16-bit Philips TDM without MCLK. Capture/playback have independent handles and share reference-counted codec/I2S resources; the PA follows playback.

The shared PCM buffer uses PSRAM for its FIFO, staging data and worker stack while synchronization state remains internal. The owner supplies bounded sink writes, waits for worker acknowledgement before releasing resources and retains pending resources when stop times out. Finite prompts wait for FIFO and DMA-tail drain before stopping playback.

Tracked Ogg prompts/digits are build inputs. CMake calls ffmpeg to generate 16 kHz mono S16LE in the selected build directory and embeds that PCM. Firmware performs no Ogg/Opus decoding and links no S3 decoder binary. Keep the source assets and their hashes/licenses even though the runtime decoder was removed.

The S31 RTSA package uses **60 ms** jitter-buffer frames. It is separate from the historical 20 ms Opus encoding of the source Ogg assets and from the driver's sink-write chunk size. Configurations of 20/40 ms are rejected without a matching RTSA package.

The user confirmed Korvo basic audio and media/face-to-face operation, Mosaico standalone UAC recording/playback, and basic bidirectional meeting translation after the PSRAM-stack revision (`e279926`). Later recovery fixes have host/build coverage only. Detailed reference/microphone routing, quantitative quality, echo tuning and endurance remain outside this update. Host coverage lives in `tests/korvo_s31_audio` and `tests/pcm_playback_buffer`; actual results are recorded in [validation](s31-validation.md). For bus pins and controls see [the Mosaico board guide](esp-mosaico-v1.2.zh-CN.md).
