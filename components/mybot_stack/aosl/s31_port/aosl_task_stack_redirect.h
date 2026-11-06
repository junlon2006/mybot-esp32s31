/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_AOSL_TASK_STACK_REDIRECT_H
#define MYBOT_AOSL_TASK_STACK_REDIRECT_H

#include "aosl_task_stack.h"

/* Force-included only by the upstream AOSL FreeRTOS thread adapter. This covers
 * both its entry wrapper's normal return and aosl_hal_thread_exit(). */
#define xTaskCreate mybot_aosl_task_create
#define vTaskDelete mybot_aosl_task_delete

#endif
