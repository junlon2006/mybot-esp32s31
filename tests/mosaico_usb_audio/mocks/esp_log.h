/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_ESP_LOG_H_
#define MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_ESP_LOG_H_

void test_usb_log(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI test_usb_log

#endif /* MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_ESP_LOG_H_ */
