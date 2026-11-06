/* SPDX-License-Identifier: Apache-2.0 */
#include "aosl_task_stack.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/idf_additions.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "aosl_tasks";

static bool psram_task(const char *name) {
    /* Audited project workers handle PCM, HTTPS/JSON or RTC event forwarding.
     * Service results return to control_mpq before persistence. That control
     * owner writes NVS and keeps its internal stack, as do opaque RTSA tasks. */
    return name &&
           (!strcmp(name, "cap_mpq") || !strcmp(name, "pb_mpq") || !strcmp(name, "mybot_mpq") ||
            !strcmp(name, "service_mpq") || !strcmp(name, "mybot_rtc"));
}

BaseType_t mybot_aosl_task_create(TaskFunction_t entry, const char *name,
                                  configSTACK_DEPTH_TYPE stack_bytes, void *argument,
                                  UBaseType_t priority, TaskHandle_t *task) {
    const bool external = psram_task(name);
    BaseType_t result = external ? xTaskCreateWithCaps(entry, name, stack_bytes, argument, priority,
                                                       task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                 : xTaskCreate(entry, name, stack_bytes, argument, priority, task);
    if (result == pdPASS) {
        ESP_LOGI(TAG, "event=task_create name=%s stack_bytes=%u memory=%s result=ok",
                 name ? name : "<unnamed>", (unsigned)stack_bytes, external ? "psram" : "internal");
    } else {
        ESP_LOGE(TAG,
                 "event=task_create name=%s stack_bytes=%u memory=%s result=failed error=%d "
                 "internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 name ? name : "<unnamed>", (unsigned)stack_bytes, external ? "psram" : "internal",
                 (int)result,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return result;
}

void mybot_aosl_task_delete(TaskHandle_t task) {
    TaskHandle_t target = task ? task : xTaskGetCurrentTaskHandle();
    /* Select the matching allocator from the actual stack, not the task name.
     * IDF's WithCaps self-delete helper reclaims its stack and internal TCB. */
    if (esp_ptr_external_ram(xTaskGetStackStart(target))) {
        vTaskDeleteWithCaps(task);
    } else {
        vTaskDelete(task);
    }
}
