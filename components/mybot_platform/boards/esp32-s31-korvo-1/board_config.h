/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "driver/gpio.h"
#define MYBOT_BOARD_NAME "esp32-s31-korvo-1"
#define MYBOT_BOARD_HAS_DISPLAY 1
#define MYBOT_BOARD_HAS_TOUCH 1
#define MYBOT_AUDIO_SAMPLE_RATE 16000
#define MYBOT_AUDIO_DEFAULT_VOLUME 60
#define MYBOT_AUDIO_I2S_MCLK GPIO_NUM_2
#define MYBOT_AUDIO_I2S_BCLK GPIO_NUM_3
#define MYBOT_AUDIO_I2S_WS GPIO_NUM_4
#define MYBOT_AUDIO_I2S_DOUT GPIO_NUM_5
#define MYBOT_AUDIO_I2S_DIN GPIO_NUM_6
#define MYBOT_AUDIO_PA_GPIO GPIO_NUM_7
#define MYBOT_DISPLAY_WIDTH 800
#define MYBOT_DISPLAY_HEIGHT 480
#define MYBOT_DISPLAY_PCLK_HZ 26000000
#define MYBOT_DISPLAY_RGB_DATA_PINS                                                                \
    {                                                                                              \
        GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14,   \
            GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_33,          \
            GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36                                                  \
    }
#define MYBOT_DISPLAY_PCLK GPIO_NUM_40
#define MYBOT_DISPLAY_DE GPIO_NUM_43
#define MYBOT_DISPLAY_HSYNC GPIO_NUM_44
#define MYBOT_DISPLAY_VSYNC GPIO_NUM_45
#define MYBOT_DISPLAY_HSYNC_PULSE 1
#define MYBOT_DISPLAY_HSYNC_BACK_PORCH 40
#define MYBOT_DISPLAY_HSYNC_FRONT_PORCH 20
#define MYBOT_DISPLAY_VSYNC_PULSE 1
#define MYBOT_DISPLAY_VSYNC_BACK_PORCH 10
#define MYBOT_DISPLAY_VSYNC_FRONT_PORCH 5
