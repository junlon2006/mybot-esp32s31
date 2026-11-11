/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_STORAGE_INIT_MOCKS_ESP_LOG_H_
#define MYBOT_TEST_STORAGE_INIT_MOCKS_ESP_LOG_H_

void test_storage_log(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) test_storage_log(tag, __VA_ARGS__)

#endif /* MYBOT_TEST_STORAGE_INIT_MOCKS_ESP_LOG_H_ */
