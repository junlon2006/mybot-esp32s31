/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_SEMPHR_H_
#define MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_SEMPHR_H_

#include "FreeRTOS.h"
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    unsigned int count;
    int dynamic;
} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

#endif /* MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_SEMPHR_H_ */
