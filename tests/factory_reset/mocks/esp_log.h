/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_FACTORY_RESET_MOCKS_ESP_LOG_H_
#define MYBOT_TEST_FACTORY_RESET_MOCKS_ESP_LOG_H_

void test_factory_reset_log(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) test_factory_reset_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) test_factory_reset_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_factory_reset_log(tag, __VA_ARGS__)

#endif /* MYBOT_TEST_FACTORY_RESET_MOCKS_ESP_LOG_H_ */
