# SPDX-License-Identifier: Apache-2.0
# Header-only registration for the unchanged, manager-resolved UAC package.
idf_component_get_property(mybot_uac_dir espressif__usb_device_uac COMPONENT_OVERRIDEN_DIR)
idf_component_register(INCLUDE_DIRS "${mybot_uac_dir}/tusb_uac"
    KCONFIG "${mybot_uac_dir}/Kconfig")
