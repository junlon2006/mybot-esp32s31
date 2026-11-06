/* SPDX-License-Identifier: Apache-2.0 */
#include "usb/mosaico_usb_descriptors.h"
#include "esp_mac.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t factory_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static esp_err_t mac_result = ESP_OK;
static unsigned mac_reads;

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    assert(mac && type == ESP_MAC_EFUSE_FACTORY);
    ++mac_reads;
    if (mac_result == ESP_OK)
        memcpy(mac, factory_mac, sizeof(factory_mac));
    return mac_result;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static void validate(const uint8_t *configuration, bool high_speed) {
    assert(configuration[0] == 9 && configuration[1] == TUSB_DESC_CONFIGURATION);
    assert(configuration[4] == 5 && configuration[5] == 1);
    assert(configuration[7] == 0x80 && configuration[8] == 250); /* Bus-powered, 500 mA. */
    size_t total = le16(configuration + 2);
    unsigned interfaces[5] = {0};
    unsigned endpoints[256] = {0};
    unsigned iad_count = 0, format_count = 0;
    uint8_t itf = 255, alt = 255;
    size_t offset = 0;
    while (offset < total) {
        const uint8_t *d = configuration + offset;
        assert(d[0] >= 2 && d[0] <= total - offset);
        switch (d[1]) {
        case TUSB_DESC_INTERFACE_ASSOCIATION:
            assert(d[0] == 8);
            if (iad_count == 0)
                assert(d[2] == 0 && d[3] == 3 && d[4] == TUSB_CLASS_AUDIO);
            else
                assert(iad_count == 1 && d[2] == 3 && d[3] == 2 && d[4] == TUSB_CLASS_CDC);
            ++iad_count;
            break;
        case TUSB_DESC_INTERFACE:
            assert(d[0] == 9 && d[2] < 5 && d[3] < 2);
            itf = d[2];
            alt = d[3];
            assert(!(interfaces[itf] & (1U << alt)));
            interfaces[itf] |= 1U << alt;
            if (itf < 3) {
                assert(d[5] == TUSB_CLASS_AUDIO);
                assert(d[6] == (itf ? AUDIO_SUBCLASS_STREAMING : AUDIO_SUBCLASS_CONTROL));
                assert(d[4] == (alt ? (itf == 1 ? 2 : 1) : 0));
            } else {
                assert(alt == 0 && d[5] == (itf == 3 ? TUSB_CLASS_CDC : TUSB_CLASS_CDC_DATA));
                assert(d[4] == (itf == 3 ? 1 : 2));
            }
            break;
        case TUSB_DESC_ENDPOINT: {
            assert(d[0] >= 7 && ++endpoints[d[2]] == 1);
            uint16_t packet = le16(d + 4);
            switch (d[2]) {
            case MYBOT_USB_EP_AUDIO_OUT:
                assert(itf == 1 && alt == 1 && d[3] == 0x05 && packet == 196 && d[6] == 1);
                break;
            case MYBOT_USB_EP_AUDIO_FEEDBACK:
                assert(itf == 1 && alt == 1 && d[3] == 0x11 && packet == 4);
                assert(d[6] == (high_speed ? 4 : 1));
                break;
            case MYBOT_USB_EP_AUDIO_IN:
                assert(itf == 2 && alt == 1 && d[3] == 0x05 && packet == 196 && d[6] == 1);
                break;
            case MYBOT_USB_EP_CDC_NOTIF:
                assert(itf == 3 && alt == 0 && d[3] == TUSB_XFER_INTERRUPT && packet == 8);
                break;
            case MYBOT_USB_EP_CDC_OUT:
            case MYBOT_USB_EP_CDC_IN:
                assert(itf == 4 && alt == 0 && d[3] == TUSB_XFER_BULK);
                assert(packet == (high_speed ? 512 : 64));
                break;
            default:
                assert(!"unexpected endpoint");
            }
            break;
        }
        case TUSB_DESC_CS_INTERFACE:
            if (itf > 0 && itf < 3 && d[2] == AUDIO_CS_AS_INTERFACE_FORMAT_TYPE) {
                assert(d[0] == 6 && d[3] == AUDIO_FORMAT_TYPE_I && d[4] == 2 && d[5] == 16);
                ++format_count;
            }
            if (itf > 0 && itf < 3 && d[2] == AUDIO_CS_AS_INTERFACE_AS_GENERAL)
                assert(d[10] == 2); /* Two physical channels on each USB stream. */
            break;
        default:
            break;
        }
        offset += d[0];
    }
    assert(offset == total && iad_count == 2 && format_count == 2);
    assert(interfaces[0] == 1 && interfaces[1] == 3 && interfaces[2] == 3 && interfaces[3] == 1 &&
           interfaces[4] == 1);
    assert(endpoints[0x01] && endpoints[0x81] && endpoints[0x82] && endpoints[0x83] &&
           endpoints[0x04] && endpoints[0x84]);
}

int main(void) {
    assert(CFG_TUD_AUDIO == 1 && CFG_TUD_CDC == 1 && CFG_TUD_AUDIO_FUNC_1_N_AS_INT == 2);
    assert(TUD_AUDIO_PREFER_RING_BUFFER && !CFG_TUD_DWC2_DMA_ENABLE && CFG_TUD_DWC2_SLAVE_ENABLE);
    assert(!CFG_TUD_MEM_DCACHE_ENABLE);
    assert(mybot_mosaico_usb_descriptors_configure(NULL) == ESP_ERR_INVALID_ARG);
    assert(!mac_reads);
    tinyusb_config_t config = {.caller_owned_marker = 123};
    assert(mybot_mosaico_usb_descriptors_configure(&config) == ESP_OK);
    assert(config.caller_owned_marker == 123 && config.descriptor.string_count == 8);
    assert(!config.descriptor.device && !config.descriptor.qualifier);
    for (size_t i = 0; i < config.descriptor.string_count; ++i)
        assert(config.descriptor.string[i]);
    assert(!strcmp(config.descriptor.string[4], "MyBot Mosaico Audio"));
    assert(!strcmp(config.descriptor.string[2], "MyBot Mosaico"));
    assert(!strcmp(config.descriptor.string[3], "MOSAICO-021122334455"));
    validate(config.descriptor.full_speed_config, false);
    validate(config.descriptor.high_speed_config, true);
    const uint8_t *high_speed = config.descriptor.high_speed_config;
    assert(mybot_mosaico_usb_descriptors_configure(&config) == ESP_OK);
    assert(high_speed == config.descriptor.high_speed_config);
    validate(config.descriptor.high_speed_config, true);
    assert(!strcmp(config.descriptor.string[3], "MOSAICO-021122334455"));
    char first_serial[32];
    strcpy(first_serial, config.descriptor.string[3]);
    factory_mac[5] = 0xa6;
    assert(mybot_mosaico_usb_descriptors_configure(&config) == ESP_OK);
    assert(!strcmp(config.descriptor.string[3], "MOSAICO-0211223344A6"));
    assert(strcmp(first_serial, config.descriptor.string[3]));
    mac_result = -1;
    tinyusb_config_t failed = {.caller_owned_marker = 456};
    assert(mybot_mosaico_usb_descriptors_configure(&failed) == mac_result);
    assert(failed.caller_owned_marker == 456 && !failed.descriptor.string);
    mac_result = ESP_OK;
    memset(factory_mac, 0, sizeof(factory_mac));
    assert(mybot_mosaico_usb_descriptors_configure(&failed) == ESP_ERR_INVALID_STATE);
    assert(!failed.descriptor.string);
    memset(factory_mac, 0xff, sizeof(factory_mac));
    assert(mybot_mosaico_usb_descriptors_configure(&failed) == ESP_ERR_INVALID_STATE);
    assert(!failed.descriptor.string);
    puts("PASS Mosaico UAC+CDC factory identity, descriptors, endpoints and shared PIO "
         "configuration");
    return 0;
}
