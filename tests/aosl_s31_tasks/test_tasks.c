/* SPDX-License-Identifier: Apache-2.0 */
#include "aosl_task_stack.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include <api/aosl_mm.h>
#include <hal/aosl_hal_thread.h>

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mock_task {
    TaskFunction_t entry;
    void *argument;
    const char *name;
    StackType_t *stack;
    bool external;
    bool suspended;
};

static TaskHandle_t current;
static TaskHandle_t created;
static jmp_buf exit_point;
static jmp_buf reaper_exit;
static struct mock_task reaper;
static QueueHandle_t reaper_queue;
static atomic_uint live_allocations;
static unsigned deleted_normal;
static unsigned deleted_caps;
static unsigned created_normal;
static atomic_uint created_caps;
static unsigned fail_task_stage;
static bool fail_wrapper;
static bool explicit_exit;
static bool explicit_handle;
static bool entered;
static bool fail_queue_start, fail_reaper_start, deny_allocations, batch_exit;
static bool fail_send_once;
static atomic_bool hold_reaper_start, release_reaper_start;
static atomic_uint reaper_start_waits;
static unsigned queue_creates, reaper_creates, queue_full_waits;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static char last_log[512];
static configSTACK_DEPTH_TYPE last_stack_bytes;
static UBaseType_t last_priority;

void *aosl_malloc(size_t size) {
    assert(!deny_allocations);
    if (fail_wrapper) {
        return NULL;
    }
    void *ptr = malloc(size);
    assert(ptr);
    ++live_allocations;
    return ptr;
}

void *aosl_calloc(size_t count, size_t size) {
    void *ptr = aosl_malloc(count * size);
    if (ptr) {
        memset(ptr, 0, count * size);
    }
    return ptr;
}

void aosl_free(void *ptr) {
    if (ptr) {
        assert(live_allocations > 0);
        --live_allocations;
        free(ptr);
    }
}

void mock_log(const char *tag, const char *format, ...) {
    (void)tag;
    va_list arguments;
    pthread_mutex_lock(&log_lock);
    va_start(arguments, format);
    vsnprintf(last_log, sizeof(last_log), format, arguments);
    va_end(arguments);
    pthread_mutex_unlock(&log_lock);
}

size_t heap_caps_get_free_size(uint32_t caps) {
    return caps & MALLOC_CAP_SPIRAM ? 200000 : 10000;
}

size_t heap_caps_get_largest_free_block(uint32_t caps) {
    return caps & MALLOC_CAP_SPIRAM ? 100000 : 4000;
}

bool esp_ptr_external_ram(const void *ptr) {
    assert(created && ptr == created->stack);
    return created->external;
}

static BaseType_t create_task(TaskFunction_t entry, const char *name,
                              configSTACK_DEPTH_TYPE stack_bytes, void *argument,
                              UBaseType_t priority, TaskHandle_t *task, bool external) {
    assert(!created || batch_exit);
    pthread_mutex_lock(&log_lock);
    last_stack_bytes = stack_bytes;
    last_priority = priority;
    pthread_mutex_unlock(&log_lock);
    if (fail_task_stage == 1) {
        return pdFAIL;
    }
    TaskHandle_t next = aosl_calloc(1, sizeof(*next));
    if (fail_task_stage == 2) {
        aosl_free(next);
        return pdFAIL;
    }
    next->stack = aosl_malloc(stack_bytes);
    next->entry = entry;
    next->argument = argument;
    next->name = name;
    next->external = external;
    created = next;
    *task = next;
    return pdPASS;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, configSTACK_DEPTH_TYPE bytes,
                       void *argument, UBaseType_t priority, TaskHandle_t *task) {
    ++created_normal;
    return create_task(entry, name, bytes, argument, priority, task, false);
}

BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char *name, configSTACK_DEPTH_TYPE bytes,
                               void *argument, UBaseType_t priority, TaskHandle_t *task,
                               UBaseType_t caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(reaper_queue && reaper.entry); /* Ready before any PSRAM worker is exposed. */
    ++created_caps;
    return create_task(entry, name, bytes, argument, priority, task, true);
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, configSTACK_DEPTH_TYPE bytes,
                               void *argument, UBaseType_t priority, StackType_t *stack,
                               StaticTask_t *storage) {
    assert(!deny_allocations && storage && !strcmp(name, "aosl_reaper"));
    assert(bytes >= 2048 && priority < configMAX_PRIORITIES);
    assert((uintptr_t)stack % portBYTE_ALIGNMENT == 0);
    ++reaper_creates;
    if (fail_reaper_start)
        return NULL;
    if (hold_reaper_start)
        while (!release_reaper_start)
            sched_yield();
    reaper = (struct mock_task){.entry = entry, .argument = argument, .stack = stack};
    return &reaper;
}

QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t item_size, uint8_t *items,
                                 StaticQueue_t *storage) {
    assert(!deny_allocations && capacity == 16 && item_size == sizeof(TaskHandle_t));
    ++queue_creates;
    if (fail_queue_start)
        return NULL;
    *storage = (StaticQueue_t){.capacity = capacity, .item_size = item_size, .items = items};
    return reaper_queue = storage;
}

void vQueueDelete(QueueHandle_t queue) {
    assert(queue == reaper_queue && !queue->count);
    reaper_queue = NULL;
}

static void run_reaper(void) {
    TaskHandle_t previous = current;
    current = &reaper;
    if (setjmp(reaper_exit) == 0)
        reaper.entry(reaper.argument);
    current = previous;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    assert(queue == reaper_queue && wait == portMAX_DELAY);
    if (fail_send_once) {
        fail_send_once = false;
        return pdFAIL;
    }
    if (queue->count == queue->capacity) {
        ++queue_full_waits;
        run_reaper(); /* Model the blocking send yielding to the permanent owner. */
    }
    assert(queue->count < queue->capacity);
    unsigned at = (queue->head + queue->count) % queue->capacity;
    memcpy(queue->items + at * queue->item_size, item, queue->item_size);
    ++queue->count;
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    assert(current == &reaper && queue == reaper_queue && wait == portMAX_DELAY);
    if (!queue->count)
        longjmp(reaper_exit, 1); /* Stop this deterministic scheduler slice. */
    memcpy(item, queue->items + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    return pdPASS;
}

void vTaskSuspend(TaskHandle_t task) {
    assert(!task && current && current != &reaper);
    current->suspended = true;
    current = NULL;
    longjmp(exit_point, 1);
}

void vTaskDelay(TickType_t ticks) {
    assert(ticks == 1);
    ++reaper_start_waits;
    sched_yield();
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    return current;
}

const char *pcTaskGetName(TaskHandle_t task) {
    return task->name;
}

StackType_t *xTaskGetStackStart(TaskHandle_t task) {
    assert(task);
    return task->stack;
}

static void delete_task(TaskHandle_t task, bool caps) {
    TaskHandle_t target = task ? task : current;
    assert(target && target->external == caps);
    if (caps)
        assert(target != current && target->suspended);
    bool latest = target == created;
    aosl_free(target->stack);
    aosl_free(target);
    if (latest)
        created = NULL;
    if (!caps) {
        current = NULL;
        longjmp(exit_point, 1);
    }
}

void vTaskDelete(TaskHandle_t task) {
    ++deleted_normal;
    delete_task(task, false);
}

void vTaskDeleteWithCaps(TaskHandle_t task) {
    ++deleted_caps;
    delete_task(task, true);
}

static void *entry(void *argument) {
    assert(argument == &entered);
    if (!batch_exit)
        assert(live_allocations == 2); /* Upstream wrapper already freed its argument. */
    entered = true;
    if (explicit_handle)
        mybot_aosl_task_delete(xTaskGetCurrentTaskHandle());
    if (explicit_exit) {
        aosl_hal_thread_exit(NULL);
        assert(!"thread_exit returned");
    }
    return argument;
}

static void run_task(const char *name, bool external, bool exits, unsigned stack_bytes) {
    unsigned normal_before = created_normal;
    unsigned caps_before = created_caps;
    unsigned deleted_before = external ? deleted_caps : deleted_normal;
    aosl_thread_t thread = 0;
    aosl_thread_param_t parameters = {.name = name, .stack_size = (int)stack_bytes};
    entered = false;
    explicit_exit = exits;
    assert(aosl_hal_thread_create(&thread, &parameters, entry, &entered) == 0);
    assert(last_stack_bytes == stack_bytes && last_priority == 5);
    assert(created_normal == normal_before + !external);
    assert(created_caps == caps_before + external);
    assert((TaskHandle_t)thread == created && live_allocations == 3);
    assert(strstr(last_log, external ? "memory=psram" : "memory=internal"));
    /* Deletion must use memory ownership even if the task label changes. */
    created->name = external ? "control_mpq" : "cap_mpq";
    current = created;
    if (setjmp(exit_point) == 0) {
        created->entry(created->argument);
        assert(!"task wrapper returned after deletion");
    }
    if (external) {
        assert(created && created->suspended && reaper_queue->count == 1);
        run_reaper();
    }
    assert(entered && live_allocations == 0);
    assert((external ? deleted_caps : deleted_normal) == deleted_before + 1);
}

static void *concurrent_failed_create(void *argument) {
    (void)argument;
    aosl_thread_t thread = 123;
    aosl_thread_param_t parameters = {.name = "cap_mpq", .stack_size = 16384};
    assert(aosl_hal_thread_create(&thread, &parameters, entry, NULL) == -ENOMEM);
    assert(thread == 123);
    return NULL;
}

static void test_reaper_startup(void) {
    aosl_thread_t thread = 123;
    aosl_thread_param_t parameters = {.name = "cap_mpq", .stack_size = 16384};
    fail_queue_start = true;
    assert(aosl_hal_thread_create(&thread, &parameters, entry, NULL) == -ENOMEM);
    assert(thread == 123 && !live_allocations && !created_caps && !reaper_creates);
    fail_queue_start = false;
    fail_reaper_start = true;
    assert(aosl_hal_thread_create(&thread, &parameters, entry, NULL) == -ENOMEM);
    assert(thread == 123 && !live_allocations && !created_caps && !reaper_queue);
    fail_reaper_start = false;

    /* Two creators overlap while the one static reaper is still initializing.
     * Fail worker allocation afterward so no task runs before this test ends. */
    hold_reaper_start = true;
    fail_task_stage = 1;
    pthread_t creators[2];
    assert(pthread_create(&creators[0], NULL, concurrent_failed_create, NULL) == 0);
    assert(pthread_create(&creators[1], NULL, concurrent_failed_create, NULL) == 0);
    while (!reaper_start_waits)
        sched_yield();
    release_reaper_start = true;
    for (unsigned i = 0; i < 2; ++i)
        assert(pthread_join(creators[i], NULL) == 0);
    hold_reaper_start = false;
    fail_task_stage = 0;
    assert(reaper_creates == 2 && queue_creates == 3 && !live_allocations && !created);
}

static void test_bounded_reaper_queue(void) {
    unsigned deleted_before = deleted_caps;
    batch_exit = true;
    for (unsigned i = 0; i < 24; ++i) {
        aosl_thread_t thread = 0;
        aosl_thread_param_t parameters = {.name = "mybot_rtc", .stack_size = 8192};
        assert(aosl_hal_thread_create(&thread, &parameters, entry, &entered) == 0);
        explicit_exit = i & 1;
        current = (TaskHandle_t)thread;
        deny_allocations = true;
        if (setjmp(exit_point) == 0)
            current->entry(current->argument);
        deny_allocations = false;
        assert(reaper_queue->count <= reaper_queue->capacity);
    }
    deny_allocations = true;
    run_reaper();
    deny_allocations = false;
    batch_exit = false;
    assert(queue_full_waits == 1 && deleted_caps == deleted_before + 24);
    assert(!created && !live_allocations && !reaper_queue->count);
    assert(reaper_creates == 2 && queue_creates == 3);
}

static void unstarted_entry(void *argument) {
    (void)argument;
}

static void test_explicit_owner_delete(void) {
    TaskHandle_t task;
    assert(mybot_aosl_task_create(unstarted_entry, "mybot_rtc", 8192, NULL, 5, &task) == pdPASS);
    task->name = "control_mpq";
    task->suspended = true;
    current = &reaper;
    unsigned previous = deleted_caps;
    deny_allocations = true;
    mybot_aosl_task_delete(task);
    deny_allocations = false;
    current = NULL;
    assert(deleted_caps == previous + 1 && !created && !live_allocations);
}

int main(void) {
    test_reaper_startup();
    const char *external[] = {"cap_mpq", "pb_mpq", "mybot_mpq", "service_mpq", "mybot_rtc"};
    const char *internal[] = {"control_mpq", "AgoraRtc",     "AgoraRtcCb", "cap_mpq_extra",
                              "service_mp",  "service_mpqq", "mybot_rt",   "mybot_rtcx",
                              "mybot_RTC",   "other",        "",           NULL};
    for (unsigned exits = 0; exits < 2; ++exits) {
        for (unsigned i = 0; i < sizeof(external) / sizeof(external[0]); ++i) {
            run_task(external[i], true, exits, !strcmp(external[i], "mybot_rtc") ? 8192 : 16384);
        }
        for (unsigned i = 0; i < sizeof(internal) / sizeof(internal[0]); ++i) {
            run_task(internal[i], false, exits, 16384);
        }
    }
    explicit_handle = true;
    fail_send_once = true;
    run_task("mybot_rtc", true, false, 8192);
    assert(!fail_send_once);
    explicit_handle = false;
    test_bounded_reaper_queue();
    test_explicit_owner_delete();
    for (unsigned external = 0; external < 2; ++external) {
        for (unsigned stage = 1; stage <= 2; ++stage) {
            aosl_thread_t thread = 123;
            aosl_thread_param_t parameters = {.name = external ? "cap_mpq" : "AgoraRtc",
                                              .stack_size = 16384};
            fail_task_stage = stage;
            assert(aosl_hal_thread_create(&thread, &parameters, entry, NULL) == -ENOMEM);
            assert(thread == 123 && live_allocations == 0 && created == NULL);
            assert(strstr(last_log, "result=failed error=0"));
            assert(strstr(last_log, "internal_free=10000 internal_largest=4000"));
            assert(strstr(last_log, "psram_free=200000 psram_largest=100000"));
        }
    }
    fail_task_stage = 0;
    fail_wrapper = true;
    aosl_thread_t thread = 123;
    aosl_thread_param_t parameters = {.name = "cap_mpq", .stack_size = 0};
    unsigned create_count = created_normal + created_caps;
    assert(aosl_hal_thread_create(&thread, &parameters, entry, NULL) == -ENOMEM);
    assert(created_normal + created_caps == create_count && live_allocations == 0);
    puts("PASS aosl_s31_tasks static reaper, concurrent startup, startup/worker failures, "
         "return/exit under OOM, queue backpressure, owner deletion and allocation policy");
    return 0;
}
