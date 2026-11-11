/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_KV_STORE_MOCKS_ESP_LOG_H_
#define MYBOT_TEST_KV_STORE_MOCKS_ESP_LOG_H_

void test_log(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGE test_log
#define ESP_LOGW test_log
#define ESP_LOGI test_log

#endif /* MYBOT_TEST_KV_STORE_MOCKS_ESP_LOG_H_ */
