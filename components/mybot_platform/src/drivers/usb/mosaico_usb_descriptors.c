/* SPDX-License-Identifier: Apache-2.0 */
#include "usb/mosaico_usb_descriptors.h"

#include "esp_mac.h"
#include "mosaico_uac_descriptors.h"

#include <stddef.h>
#include <stdio.h>

#define MOSAICO_USB_MAX_POWER_MA 500
#define MYBOT_USB_CONFIGURATION_BYTES                                                              \
    (TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO_FUNC_1_DESC_LEN + TUD_CDC_DESC_LEN)

/* Audio endpoints keep the addresses verified by the standalone UAC test.
 * CDC adds independent endpoints and does not start another USB controller. */
#define MYBOT_USB_CONFIGURATION(cdc_packet)                                                        \
    TUD_CONFIG_DESCRIPTOR(1, MYBOT_USB_ITF_COUNT, 0, MYBOT_USB_CONFIGURATION_BYTES, 0,             \
                          MOSAICO_USB_MAX_POWER_MA),                                               \
        MYBOT_MOSAICO_UAC_DESCRIPTOR(MYBOT_USB_ITF_AUDIO_CONTROL, 4, MYBOT_USB_EP_AUDIO_OUT,       \
                                     MYBOT_USB_EP_AUDIO_IN, MYBOT_USB_EP_AUDIO_FEEDBACK),          \
        TUD_CDC_DESCRIPTOR(MYBOT_USB_ITF_CDC, 7, MYBOT_USB_EP_CDC_NOTIF, 8, MYBOT_USB_EP_CDC_OUT,  \
                           MYBOT_USB_EP_CDC_IN, cdc_packet)

static const uint8_t s_full_speed[] = {MYBOT_USB_CONFIGURATION(64)};
static uint8_t s_high_speed[] = {MYBOT_USB_CONFIGURATION(512)};
static char s_serial[sizeof("MOSAICO-") + 12];
static const char *s_strings[] = {
    (const char[]){0x09, 0x04}, "MyBot",   "MyBot Mosaico", s_serial,
    "MyBot Mosaico Audio",      "Speaker", "Microphone",    "Console",
};

_Static_assert(sizeof(s_full_speed) == MYBOT_USB_CONFIGURATION_BYTES,
               "Full-speed descriptor length mismatch");
_Static_assert(sizeof(s_high_speed) == MYBOT_USB_CONFIGURATION_BYTES,
               "High-speed descriptor length mismatch");
_Static_assert(CFG_TUD_AUDIO == 1 && CFG_TUD_CDC == 1,
               "Descriptors require one duplex UAC and one CDC function");
_Static_assert(CFG_TUD_AUDIO_FUNC_1_N_AS_INT == 2, "Duplex UAC has two streaming interfaces");
_Static_assert(TUD_AUDIO_DESC_MIC_FEATURE_UNIT_N_CHANNEL_LEN == 18,
               "Fixed stereo graph omits one 18-byte microphone feature unit");
_Static_assert(MYBOT_MOSAICO_UAC_CONTROL_DESC_LEN ==
                   TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_SPK_FEATURE_UNIT_N_CHANNEL_LEN +
                       2 * (TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN),
               "Audio control graph length mismatch");

esp_err_t mybot_mosaico_usb_descriptors_configure(tinyusb_config_t *config) {
    if (!config)
        return ESP_ERR_INVALID_ARG;
    /* Read the six-byte factory identity, independent of any Wi-Fi MAC override.
     * esp_efuse_mac_get_default() can return eight bytes on S31; select MAC-48
     * explicitly rather than giving that API a six-byte buffer. */
    uint8_t mac[6];
    esp_err_t result = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    if (result != ESP_OK)
        return result;
    unsigned nonzero = 0;
    for (size_t i = 0; i < sizeof(mac); ++i)
        nonzero |= mac[i];
    if (!nonzero || (mac[0] & 1U))
        return ESP_ERR_INVALID_STATE;
    int length = snprintf(s_serial, sizeof(s_serial), "MOSAICO-%02X%02X%02X%02X%02X%02X",
                          (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3],
                          (unsigned)mac[4], (unsigned)mac[5]);
    if (length != (int)sizeof(s_serial) - 1)
        return ESP_ERR_INVALID_STATE;
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
