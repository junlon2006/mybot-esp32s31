/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_CODEC_AUDIO_MOCKS_DRIVER_GPIO_H_
#define MYBOT_TEST_CODEC_AUDIO_MOCKS_DRIVER_GPIO_H_

#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define GPIO_NUM_2 2
#define GPIO_NUM_3 3
#define GPIO_NUM_4 4
#define GPIO_NUM_5 5
#define GPIO_NUM_6 6
#define GPIO_NUM_7 7
esp_err_t gpio_set_level(gpio_num_t pin, unsigned level);
#define GPIO_NUM_9 9
#define GPIO_NUM_35 35
#define GPIO_NUM_36 36
#define GPIO_NUM_37 37
#define GPIO_NUM_40 40
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_45 45
#define GPIO_NUM_49 49
#define GPIO_NUM_50 50
#define GPIO_NUM_51 51
#define GPIO_NUM_52 52
#define GPIO_NUM_54 54
#define GPIO_NUM_56 56
#define GPIO_NUM_57 57
#define GPIO_NUM_60 60

#endif /* MYBOT_TEST_CODEC_AUDIO_MOCKS_DRIVER_GPIO_H_ */
