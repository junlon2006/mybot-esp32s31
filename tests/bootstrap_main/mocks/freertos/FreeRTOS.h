/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_FREERTOS_FREERTOS_H_
#define MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_FREERTOS_FREERTOS_H_

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))

#endif /* MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_FREERTOS_FREERTOS_H_ */
