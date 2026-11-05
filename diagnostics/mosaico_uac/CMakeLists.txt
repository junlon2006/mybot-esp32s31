idf_component_register(
    SRCS "main.c" "audio.c" "usb_audio.c" "usb_transfer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_codec_dev esp_driver_gpio esp_driver_i2c esp_driver_i2s
    PRIV_REQUIRES esp_hw_support esp_hal_usb esp_timer)

# The pinned UAC component invokes these FreeRTOS operations from TinyUSB's ISR.
# Scope the compatibility shim to that component; managed sources stay unchanged.
idf_component_get_property(uac_lib espressif__usb_device_uac COMPONENT_LIB)
target_compile_options(${uac_lib} PRIVATE
    "-include${CMAKE_CURRENT_LIST_DIR}/uac_freertos_compat.h")

# The pinned TinyUSB linear audio OUT path stops rearming after an empty packet.
# DWC2 PIO supports direct FIFO transfers, whose RX path always rearms.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_compile_definitions(${tusb_lib} PUBLIC
    TUD_AUDIO_PREFER_RING_BUFFER=1
    CFG_TUD_DWC2_DMA_ENABLE=0
    CFG_TUD_DWC2_SLAVE_ENABLE=1)

include("${CMAKE_CURRENT_LIST_DIR}/dwc2_iso.cmake")
idf_build_set_property(LINK_OPTIONS
    "-Wl,--wrap=dcd_edpt_xfer,--wrap=dcd_event_handler,--wrap=tud_descriptor_configuration_cb" APPEND)
