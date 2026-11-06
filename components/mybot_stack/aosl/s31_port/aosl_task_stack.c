/* SPDX-License-Identifier: Apache-2.0 */
#include "aosl_task_stack.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "aosl_tasks";

#define REAPER_QUEUE_LENGTH 16
#define REAPER_STACK_BYTES 2048

/* WithCaps self-delete allocates a temporary internal task in IDF. Keep one
 * permanent owner instead, so worker exit still succeeds under heap pressure. */
static DRAM_ATTR StaticQueue_t s_reaper_queue_storage;
static DRAM_ATTR uint8_t s_reaper_items[REAPER_QUEUE_LENGTH * sizeof(TaskHandle_t)];
static DRAM_ATTR StaticTask_t s_reaper_task_storage;
static DRAM_ATTR StackType_t s_reaper_stack[REAPER_STACK_BYTES / sizeof(StackType_t)]
    __attribute__((aligned(portBYTE_ALIGNMENT)));
static DRAM_ATTR QueueHandle_t s_reaper_queue;
static DRAM_ATTR uint32_t s_reaper_state; /* 0: absent, 1: creating, 2: ready */

static void reap_tasks(void *argument) {
    QueueHandle_t queue = argument;
    for (;;) {
        TaskHandle_t task;
        if (xQueueReceive(queue, &task, portMAX_DELAY) == pdPASS) {
            /* IDF's external deletion path suspends the target and waits until
             * it is off every core before freeing its WithCaps stack and TCB. */
            vTaskDeleteWithCaps(task);
        }
    }
}

static bool prepare_reaper(void) {
    for (;;) {
        uint32_t state = __atomic_load_n(&s_reaper_state, __ATOMIC_ACQUIRE);
        if (state == 2)
            return true;
        if (state == 1) {
            vTaskDelay(1);
            continue;
        }
        uint32_t absent = 0;
        if (!__atomic_compare_exchange_n(&s_reaper_state, &absent, 1, false, __ATOMIC_ACQ_REL,
                                         __ATOMIC_ACQUIRE))
            continue;
        QueueHandle_t queue = xQueueCreateStatic(REAPER_QUEUE_LENGTH, sizeof(TaskHandle_t),
                                                 s_reaper_items, &s_reaper_queue_storage);
        if (!queue) {
            __atomic_store_n(&s_reaper_state, 0, __ATOMIC_RELEASE);
            return false;
        }
        TaskHandle_t reaper =
            xTaskCreateStatic(reap_tasks, "aosl_reaper", REAPER_STACK_BYTES, queue,
                              configMAX_PRIORITIES - 1, s_reaper_stack, &s_reaper_task_storage);
        if (!reaper) {
            vQueueDelete(queue);
            __atomic_store_n(&s_reaper_state, 0, __ATOMIC_RELEASE);
            return false;
        }
        s_reaper_queue = queue;
        __atomic_store_n(&s_reaper_state, 2, __ATOMIC_RELEASE);
        return true;
    }
}

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
    BaseType_t result;
    if (external) {
        result = prepare_reaper()
                     ? xTaskCreateWithCaps(entry, name, stack_bytes, argument, priority, task,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                     : pdFAIL;
    } else {
        result = xTaskCreate(entry, name, stack_bytes, argument, priority, task);
    }
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
    /* Select the matching allocator from the actual stack, not the task name. */
    if (esp_ptr_external_ram(xTaskGetStackStart(target))) {
        if (target != xTaskGetCurrentTaskHandle()) {
            vTaskDeleteWithCaps(target);
            return;
        }
        /* Every adapter-created PSRAM task has a ready reaper before it runs.
         * A full fixed queue applies backpressure without allocating or losing
         * ownership. The reaper can safely delete us before our final suspend. */
        while (xQueueSend(s_reaper_queue, &target, portMAX_DELAY) != pdPASS)
            vTaskDelay(1);
        for (;;)
            vTaskSuspend(NULL);
    } else {
        vTaskDelete(task);
    }
}
