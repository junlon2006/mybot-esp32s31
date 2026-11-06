/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
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
