# SPDX-License-Identifier: Apache-2.0
# IDF 6.1 builds every downloaded managed component even with require: no.
# Redirect this package's registration before component add_subdirectory(); its
# downloaded sources and metadata stay intact for the standalone diagnostic.
idf_component_get_property(mybot_uac_dir espressif__usb_device_uac COMPONENT_DIR)
if(NOT EXISTS "${mybot_uac_dir}/tusb_uac/uac_descriptors.h")
    message(FATAL_ERROR "Pinned UAC descriptor headers are unavailable")
endif()
idf_component_set_property(espressif__usb_device_uac COMPONENT_OVERRIDEN_DIR "${mybot_uac_dir}")
idf_component_set_property(espressif__usb_device_uac COMPONENT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/uac_headers")
