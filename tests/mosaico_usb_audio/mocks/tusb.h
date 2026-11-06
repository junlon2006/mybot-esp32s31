/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_USB_DEVICE_UAC_AS_PART 1
#define CONFIG_UAC_SAMPLE_RATE 48000
#define CONFIG_UAC_SPEAKER_CHANNEL_NUM 2
#define CONFIG_UAC_MIC_CHANNEL_NUM 2
#define CONFIG_UAC_BIT_RESOLUTION 16
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ 1920
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ 1920
#define TUD_AUDIO_PREFER_RING_BUFFER 1
#define CFG_TUD_DWC2_DMA_ENABLE 0
#define CFG_TUD_MEM_DCACHE_ENABLE 0
#define AUDIO_FEEDBACK_METHOD_DISABLED 0
#define XFER_RESULT_SUCCESS 0
#define XFER_RESULT_FAILED 1
typedef enum { TUSB_SPEED_INVALID, TUSB_SPEED_FULL, TUSB_SPEED_HIGH } tusb_speed_t;
typedef struct {
    int method;
    uint32_t sample_freq;
} audio_feedback_params_t;
bool tud_mounted(void);
bool tud_suspended(void);
tusb_speed_t tud_speed_get(void);
bool tud_audio_n_fb_set(uint8_t function, uint32_t value);
static inline uint32_t tu_htole32(uint32_t value) {
    return value;
}
static inline uint16_t tu_htole16(uint16_t value) {
    return value;
}
static inline void tu_unaligned_write16(void *destination, uint16_t value) {
    memcpy(destination, &value, sizeof(value));
}
static inline void tu_unaligned_write32(void *destination, uint32_t value) {
    memcpy(destination, &value, sizeof(value));
}

typedef struct {
    uint8_t bRequest;
    uint16_t wIndex, wValue, wLength;
} tusb_control_request_t;
static inline uint16_t tu_le16toh(uint16_t value) {
    return value;
}
uint16_t tud_audio_n_available(uint8_t function);
uint16_t tud_audio_n_read(uint8_t function, void *buffer, uint16_t length);
bool tud_audio_n_clear_ep_out_ff(uint8_t function);
bool tud_audio_buffer_and_schedule_control_xfer(uint8_t port, const tusb_control_request_t *request,
                                                void *data, uint16_t length);
#define AUDIO_CS_REQ_CUR 1
#define AUDIO_CS_REQ_RANGE 2
#define AUDIO_CS_CTRL_SAM_FREQ 1
#define AUDIO_CS_CTRL_CLK_VALID 2
#define AUDIO_FU_CTRL_MUTE 1
#define AUDIO_FU_CTRL_VOLUME 2

/* FIFO handle is opaque to the production microphone transport. */
typedef struct {
    unsigned unused;
} tu_fifo_t;
