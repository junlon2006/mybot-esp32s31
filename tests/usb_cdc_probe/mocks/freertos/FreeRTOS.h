/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_PROBE_FREERTOS_H
#define TEST_USB_PROBE_FREERTOS_H
#include <stdint.h>
typedef int portMUX_TYPE;
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
void test_enter_critical(portMUX_TYPE *lock);
void test_exit_critical(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) test_enter_critical(lock)
#define portEXIT_CRITICAL(lock) test_exit_critical(lock)
void test_enter_critical_isr(portMUX_TYPE *lock);
void test_exit_critical_isr(portMUX_TYPE *lock);
#define portENTER_CRITICAL_ISR(lock) test_enter_critical_isr(lock)
#define portEXIT_CRITICAL_ISR(lock) test_exit_critical_isr(lock)
#endif
