/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ESP_LOG_H
#define TEST_ESP_LOG_H
void test_log(const char *format, ...);
#define ESP_LOGI(tag, ...) test_log(__VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log(__VA_ARGS__)
#endif
