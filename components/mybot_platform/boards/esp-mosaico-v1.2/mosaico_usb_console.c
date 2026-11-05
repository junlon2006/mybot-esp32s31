/* SPDX-License-Identifier: Apache-2.0 */
#include "mosaico_usb_console.h"

#include <stdbool.h>
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"

static const char *s_strings[] = {
    (const char[]){0x09, 0x04}, "MyBot", "MyBot Mosaico", "MOSAICO-V12", "Console",
};

esp_err_t mosaico_usb_console_init(void) {
    static bool attempted;
    static esp_err_t result = ESP_ERR_INVALID_STATE;
    if (attempted)
        return result;
    attempted = true;
    /* S31 port 0 uses its native HS UTMI controller. Leave USB power/CC to
     * Mosaico's HUSB320 circuit. No UAC interface is enabled in this image. */
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.descriptor.string = s_strings;
    config.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    result = tinyusb_driver_install(&config);
    if (result != ESP_OK)
        return result;
    const tinyusb_config_cdcacm_t cdc = {.cdc_port = TINYUSB_CDC_ACM_0};
    result = tinyusb_cdcacm_init(&cdc);
    if (result != ESP_OK) {
        (void)tinyusb_driver_uninstall();
        return result;
    }
    /* The upstream CDC VFS writes nonblocking: unplugging a USB cable must not
     * block audio, networking or translation. Console ownership lasts to reboot. */
    result = tinyusb_console_init(TINYUSB_CDC_ACM_0);
    if (result == ESP_OK)
        ESP_LOGI("mosaico", "event=console_ready transport=usb_cdc");
    return result;
}
