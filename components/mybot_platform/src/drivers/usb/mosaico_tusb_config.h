/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* A single configuration must reach TinyUSB, esp_tinyusb, UAC and the board.
 * AS_PART prevents the UAC package from injecting another tusb_config.h. */
#include "tusb_config.h"
#include "tusb_config_uac.h"

/* UAC 1.3.1 still compiles a legacy tusb_init() branch even when the caller
 * skips it. Define S31 port 0 for that API; esp_tinyusb remains the owner. */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)

#if !CONFIG_USB_DEVICE_UAC_AS_PART || !CONFIG_IDF_TARGET_ESP32S31
#error "Mosaico USB requires S31 and UAC composite mode"
#endif
#if CONFIG_UAC_SAMPLE_RATE != 48000 || CONFIG_UAC_BIT_RESOLUTION != 16 ||                          \
    CONFIG_UAC_SPEAKER_CHANNEL_NUM != 2 || CONFIG_UAC_MIC_CHANNEL_NUM != 2
#error "Keep the hardware-validated 48 kHz stereo S16 UAC format"
#endif
#if CFG_TUD_CDC != 1
#error "Mosaico USB requires exactly one CDC function"
#endif
#if CFG_TUD_CDC_EP_BUFSIZE < 512 || CFG_TUD_CDC_RX_BUFSIZE < CFG_TUD_CDC_EP_BUFSIZE
#error "CDC buffers must accept a full high-speed bulk packet"
#endif

/* Preserve the validated S31 PIO/FIFO audio path. The pinned linear OUT path
 * can stop rearming after a zero-length packet. */
#undef CFG_TUD_DWC2_DMA_ENABLE
#define CFG_TUD_DWC2_DMA_ENABLE 0
#undef CFG_TUD_DWC2_SLAVE_ENABLE
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#undef CFG_TUD_MEM_DCACHE_ENABLE
#define CFG_TUD_MEM_DCACHE_ENABLE 0
#undef CFG_TUD_MEM_CACHE_ENABLE
#define CFG_TUD_MEM_CACHE_ENABLE 0
#define TUD_AUDIO_PREFER_RING_BUFFER 1

/* The duplex function has two streaming interfaces, each with alt 0 and 1. */
#undef CFG_TUD_AUDIO_FUNC_1_N_AS_INT
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 2
