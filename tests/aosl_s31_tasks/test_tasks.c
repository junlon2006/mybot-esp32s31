/* SPDX-License-Identifier: Apache-2.0 */
#include "aosl_task_stack.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/idf_additions.h"
#include <api/aosl_mm.h>
#include <hal/aosl_hal_thread.h>

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
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
};

static TaskHandle_t current;
static TaskHandle_t created;
static jmp_buf exit_point;
static unsigned live_allocations;
static unsigned deleted_normal;
static unsigned deleted_caps;
static unsigned created_normal;
static unsigned created_caps;
static unsigned fail_task_stage;
static bool fail_wrapper;
static bool explicit_exit;
static bool entered;
static char last_log[512];
static configSTACK_DEPTH_TYPE last_stack_bytes;
static UBaseType_t last_priority;

void *aosl_malloc(size_t size) {
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
    va_start(arguments, format);
    vsnprintf(last_log, sizeof(last_log), format, arguments);
    va_end(arguments);
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
    assert(!created);
    last_stack_bytes = stack_bytes;
    last_priority = priority;
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
    ++created_caps;
    return create_task(entry, name, bytes, argument, priority, task, true);
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
    assert(target && target == created && target->external == caps);
    aosl_free(target->stack);
    aosl_free(target);
    created = NULL;
    current = NULL;
    longjmp(exit_point, 1);
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
    assert(live_allocations == 2); /* Upstream wrapper already freed its argument. */
    entered = true;
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
    assert(entered && live_allocations == 0);
    assert((external ? deleted_caps : deleted_normal) == deleted_before + 1);
}

int main(void) {
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
    puts("PASS aosl_s31_tasks allocation policy, normal return, thread exit, failures and CMake "
         "scope");
    return 0;
}
