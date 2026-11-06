# SPDX-License-Identifier: Apache-2.0
# project_include.cmake replaces only the package's runtime registration with
# its pinned headers/configuration before IDF processes component directories.
idf_component_register(REQUIRES espressif__esp_tinyusb espressif__usb_device_uac)
