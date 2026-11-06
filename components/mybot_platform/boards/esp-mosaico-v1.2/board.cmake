# SPDX-License-Identifier: Apache-2.0
set(MYBOT_BOARD_TARGET "esp32s31")
set(MYBOT_BOARD_SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(MYBOT_BOARD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(MYBOT_BOARD_HAS_DISPLAY ON)
set(MYBOT_BOARD_HAS_VIDEO OFF)
set(MYBOT_BOARD_REQUIRED_CONFIGS CONFIG_PARTITION_TABLE_CUSTOM CONFIG_ESPTOOLPY_FLASHSIZE_16MB
    CONFIG_SPIRAM CONFIG_SPIRAM_MODE_OCT CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    CONFIG_CODEC_ES8311_SUPPORT CONFIG_USB_DEVICE_UAC_AS_PART)
set(MYBOT_BOARD_FORBIDDEN_CONFIGS CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE CONFIG_CODEC_ZL38063_SUPPORT CONFIG_MYBOT_ENABLE_VIDEO
    CONFIG_MYBOT_USB_CDC_PROBE CONFIG_MYBOT_USB_HOST_PROBE)
set(MYBOT_BOARD_PARTITION_TABLE "partitions/v2/16m.csv")
get_filename_component(MYBOT_PLATFORM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${MYBOT_PLATFORM_ROOT}/src/drivers/display/display.cmake")
set(MYBOT_BOARD_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/board.c"
    "${CMAKE_CURRENT_LIST_DIR}/mosaico_hardware.c"
    "${CMAKE_CURRENT_LIST_DIR}/mosaico_usb_console.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/usb/mosaico_usb_descriptors.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/usb/mosaico_usb_audio.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/usb/mosaico_usb_resampler.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/audio/codec_audio.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/audio/mosaico_codec_audio.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/input/mosaico_gpio_keys.c"
    "${MYBOT_PLATFORM_ROOT}/src/drivers/input/mosaico_cst9220_touch.c")
mybot_display_add_panel(MYBOT_BOARD_SOURCES "mosaico_co5300_panel.c")
mybot_display_add_lvgl_sources(MYBOT_BOARD_SOURCES "mosaico_lvgl_adapter.cc")
set(MYBOT_BOARD_REQUIRES espressif__esp_tinyusb espressif__usb_device_uac esp_codec_dev esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_i2s
    esp_lcd esp_mm espressif__esp_lcd_co5300 espressif__esp_lcd_touch_cst9220 esp_timer nvs_flash)
