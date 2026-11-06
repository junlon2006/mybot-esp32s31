/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
void test_capture_unlock(portMUX_TYPE *lock);
#define portENTER_CRITICAL_ISR(lock) pthread_mutex_lock(lock)
#define portEXIT_CRITICAL_ISR(lock) test_capture_unlock(lock)
