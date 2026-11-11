/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_FREERTOS_FREERTOS_H_
#define MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_FREERTOS_FREERTOS_H_

#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
void test_capture_unlock(portMUX_TYPE *lock);
#define portENTER_CRITICAL_ISR(lock) pthread_mutex_lock(lock)
#define portEXIT_CRITICAL_ISR(lock) test_capture_unlock(lock)

#endif /* MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_FREERTOS_FREERTOS_H_ */
