/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_FREERTOS_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_FREERTOS_H_

#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef unsigned configSTACK_DEPTH_TYPE;
typedef uint8_t StackType_t;
typedef uint32_t TickType_t;
typedef struct {
    uintptr_t storage[32];
} StaticTask_t;
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY UINT32_MAX
#define portBYTE_ALIGNMENT 16
#define configMAX_PRIORITIES 25

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_FREERTOS_H_ */
