/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_ESP_LOG_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_ESP_LOG_H_

void mock_log(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI mock_log
#define ESP_LOGE mock_log

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_ESP_LOG_H_ */
