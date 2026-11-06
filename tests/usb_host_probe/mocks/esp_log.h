/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_HOST_PROBE_ESP_LOG_H
#define TEST_USB_HOST_PROBE_ESP_LOG_H
void test_log(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI(tag, ...) test_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) test_log(tag, __VA_ARGS__)
#endif
