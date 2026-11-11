/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_API_AOSL_LOG_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_API_AOSL_LOG_H_

#include "esp_log.h"
#define AOSL_LOG_ERR(...) mock_log("aosl", __VA_ARGS__)

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_API_AOSL_LOG_H_ */
