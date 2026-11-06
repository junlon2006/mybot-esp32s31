# SPDX-License-Identifier: Apache-2.0
# Include after project(), for Mosaico production only, once all targets exist.
set(mybot_usb_root "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(mybot_usb_repo "${mybot_usb_root}/../../../../.." ABSOLUTE)
set(mybot_usb_diagnostic "${mybot_usb_repo}/diagnostics/mosaico_uac")

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(esp_tusb_lib espressif__esp_tinyusb COMPONENT_LIB)
idf_component_get_property(esp_tusb_dir espressif__esp_tinyusb COMPONENT_DIR)
idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_OVERRIDEN_DIR)
idf_component_get_property(mybot_usb_platform_lib mybot_platform COMPONENT_LIB)

# UAC is a header/configuration dependency; the project owns every audio callback.
foreach(mybot_usb_target IN ITEMS ${tusb_lib} ${esp_tusb_lib} ${mybot_usb_platform_lib})
    target_compile_definitions(${mybot_usb_target} PUBLIC
        CFG_TUSB_CONFIG_FILE="mosaico_tusb_config.h")
    target_include_directories(${mybot_usb_target} PUBLIC
        "${mybot_usb_root}" "${esp_tusb_dir}/include" "${uac_dir}/tusb_uac")
endforeach()

# Reuse the hash-checked DWC2 fixes from the passed standalone diagnostic.
include("${mybot_usb_diagnostic}/dwc2_iso.cmake")
# project() already copied IDF's LINK_OPTIONS property to the executable.
# Attach these options to that actual target, or --gc-sections removes the
# unused wrappers and silently disables feedback correction and wire counters.
idf_build_get_property(mybot_usb_elf EXECUTABLE)
target_link_options(${mybot_usb_elf} PRIVATE
    "-Wl,--wrap=dcd_edpt_xfer,--wrap=dcd_event_handler,--wrap=usbd_edpt_xfer_fifo")
