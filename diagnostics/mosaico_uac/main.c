/* SPDX-License-Identifier: Apache-2.0 */
#include "audio.h"
#include "usb_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    const char *tag = "mosaico_uac_test";
    ESP_LOGI(tag, "board=Mosaico_V1.2 mode=usb_audio_only rate=48000 bits=16 channels=2");
    esp_err_t result = mosaico_audio_init();
    if (result == ESP_OK) {
        esp_err_t tone_result = mosaico_audio_test_tone();
        if (tone_result != ESP_OK)
            ESP_LOGE(tag, "speaker_tone_failed error=%s", esp_err_to_name(tone_result));
        result = mosaico_usb_audio_start();
    }
    if (result != ESP_OK) {
        ESP_LOGE(tag, "startup_failed error=%s recovery=manual_boot_download",
                 esp_err_to_name(result));
        return;
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        mosaico_usb_audio_log_stats();
    }
}
