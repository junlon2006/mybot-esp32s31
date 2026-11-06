/* SPDX-License-Identifier: Apache-2.0 */
#include "storage_init.h"

#include "esp_log.h"
#include "nvs_flash.h"

int mybot_storage_init(void) {
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        /* The same partition owns Wi-Fi/auth data and the durable cloud-session
         * journal. An initialization error never proves those can be discarded. */
        ESP_LOGE("mybot_storage", "event=nvs_init result=error error=%s code=0x%x data=preserved",
                 esp_err_to_name(result), (unsigned)result);
        return -1;
    }
    return 0;
}
