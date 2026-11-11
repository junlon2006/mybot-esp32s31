/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_TASK_H_
#define MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_TASK_H_

#include "FreeRTOS.h"
typedef struct host_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef enum { eRunning, eSuspended } eTaskState;
eTaskState eTaskGetState(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void vTaskSuspend(TaskHandle_t task);

#endif /* MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_TASK_H_ */
