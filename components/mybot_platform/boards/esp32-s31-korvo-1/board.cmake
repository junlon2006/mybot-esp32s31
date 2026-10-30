# SPDX-License-Identifier: Apache-2.0
set(MYBOT_BOARD_TARGET "esp32s31")
set(MYBOT_BOARD_SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(MYBOT_BOARD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/reference")
set(MYBOT_BOARD_HAS_DISPLAY ON)
set(MYBOT_BOARD_HAS_VIDEO OFF)
set(MYBOT_BOARD_REQUIRED_CONFIGS CONFIG_PARTITION_TABLE_CUSTOM CONFIG_ESPTOOLPY_FLASHSIZE_16MB
    CONFIG_SPIRAM CONFIG_SPIRAM_MODE_OCT CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    CONFIG_CODEC_ES8389_SUPPORT)
set(MYBOT_BOARD_FORBIDDEN_CONFIGS CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE CONFIG_CODEC_ZL38063_SUPPORT CONFIG_MYBOT_ENABLE_VIDEO)
set(MYBOT_BOARD_PARTITION_TABLE "partitions/v2/16m.csv")
get_filename_component(MYBOT_PLATFORM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${MYBOT_PLATFORM_ROOT}/src/drivers/display/display.cmake")
set(MYBOT_BOARD_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/board.c"
    "${CMAKE_CURRENT_LIST_DIR}/korvo_s31_hardware.c"
    "${CMAKE_CURRENT_LIST_DIR}/reference/esp32_s31_adc_calibration.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/audio/korvo_s31_codec_audio.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/input/korvo_s31_adc_keys.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/input/korvo_s31_gt1151_touch.c")
mybot_display_add_panel(MYBOT_BOARD_SOURCES "korvo_s31_rgb_panel.c")
mybot_display_add_lvgl_sources(MYBOT_BOARD_SOURCES "korvo_s31_lvgl_adapter.cc")
set(MYBOT_BOARD_REQUIRES esp_codec_dev esp_adc esp_driver_gpio esp_driver_i2c esp_driver_i2s
    esp_lcd esp_mm espressif__esp_lcd_touch_gt1151 esp_timer nvs_flash)
