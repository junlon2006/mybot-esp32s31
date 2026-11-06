/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_HOST_PROBE_FREERTOS_H
#define TEST_USB_HOST_PROBE_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif
