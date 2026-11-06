/* SPDX-License-Identifier: Apache-2.0 */
#include "usb/mosaico_usb_descriptors.h"

#include "uac_descriptors.h"

#include <stddef.h>

#define MYBOT_USB_CONFIGURATION_BYTES                                                              \
    (TUD_CONFIG_DESC_LEN + TUD_AUDIO_DEVICE_DESC_LEN + TUD_CDC_DESC_LEN)

/* Audio endpoints keep the addresses verified by the standalone UAC test.
 * CDC adds independent endpoints and does not start another USB controller. */
#define MYBOT_USB_CONFIGURATION(cdc_packet)                                                        \
    TUD_CONFIG_DESCRIPTOR(1, MYBOT_USB_ITF_COUNT, 0, MYBOT_USB_CONFIGURATION_BYTES, 0, 100),       \
        TUD_AUDIO_DESCRIPTOR(MYBOT_USB_ITF_AUDIO_CONTROL, 4, MYBOT_USB_EP_AUDIO_OUT,               \
                             MYBOT_USB_EP_AUDIO_IN, MYBOT_USB_EP_AUDIO_FEEDBACK),                  \
        TUD_CDC_DESCRIPTOR(MYBOT_USB_ITF_CDC, 7, MYBOT_USB_EP_CDC_NOTIF, 8, MYBOT_USB_EP_CDC_OUT,  \
                           MYBOT_USB_EP_CDC_IN, cdc_packet)

static const uint8_t s_full_speed[] = {MYBOT_USB_CONFIGURATION(64)};
static uint8_t s_high_speed[] = {MYBOT_USB_CONFIGURATION(512)};
static const char *s_strings[] = {
    (const char[]){0x09, 0x04}, "MyBot",   "MyBot Mosaico", "MOSAICO-V12",
    "MyBot Mosaico Audio",      "Speaker", "Microphone",    "Console",
};

_Static_assert(sizeof(s_full_speed) == MYBOT_USB_CONFIGURATION_BYTES,
               "Full-speed descriptor length mismatch");
_Static_assert(sizeof(s_high_speed) == MYBOT_USB_CONFIGURATION_BYTES,
               "High-speed descriptor length mismatch");
_Static_assert(CFG_TUD_AUDIO == 1 && CFG_TUD_CDC == 1,
               "Descriptors require one duplex UAC and one CDC function");
_Static_assert(CFG_TUD_AUDIO_FUNC_1_N_AS_INT == 2, "Duplex UAC has two streaming interfaces");

esp_err_t mybot_mosaico_usb_descriptors_configure(tinyusb_config_t *config) {
    if (!config)
        return ESP_ERR_INVALID_ARG;
    /* usb_device_uac's descriptor macro supplies bInterval=1. At high speed
     * feedback must use 4 (1 ms), matching the passed macOS test. Other-speed
     * requests are served from these same FS/HS buffers by esp_tinyusb. */
    size_t offset = 0;
    unsigned feedback_count = 0;
    while (offset < sizeof(s_high_speed)) {
        uint8_t length = s_high_speed[offset];
        if (length < 2 || length > sizeof(s_high_speed) - offset)
            return ESP_ERR_INVALID_STATE;
        if (s_high_speed[offset + 1] == TUSB_DESC_ENDPOINT && length >= 7 &&
            s_high_speed[offset + 2] == MYBOT_USB_EP_AUDIO_FEEDBACK &&
            (s_high_speed[offset + 3] & 0x3f) == 0x11) {
            s_high_speed[offset + 6] = 4;
            ++feedback_count;
        }
        offset += length;
    }
    if (feedback_count != 1)
        return ESP_ERR_INVALID_STATE;
    /* esp_tinyusb's defaults already provide the IAD device/qualifier class.
     * Its automatic PID includes AUDIO and CDC, distinct from CDC-only images. */
    config->descriptor.device = NULL;
    config->descriptor.qualifier = NULL;
    config->descriptor.full_speed_config = s_full_speed;
    config->descriptor.high_speed_config = s_high_speed;
    config->descriptor.string = s_strings;
    config->descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    return ESP_OK;
}
