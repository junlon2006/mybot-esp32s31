/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_WIFI_PORTAL_JSON_H_
#define MYBOT_WIFI_PORTAL_JSON_H_

#include "ssid_manager.h"
#include <esp_wifi_types_generic.h>
#include <cJSON.h>
#include <memory>

namespace wifi_portal {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

inline Json SavedList(const std::vector<SsidItem> &profiles) {
    Json list(cJSON_CreateArray(), cJSON_Delete);
    for (const auto &profile : profiles) {
        Json name(cJSON_CreateString(profile.ssid.c_str()), cJSON_Delete);
        if (!cJSON_AddItemToArray(list.get(), name.get()))
            return Json(nullptr, cJSON_Delete);
        name.release();
    }
    return list;
}

inline Json ScanList(const std::vector<wifi_ap_record_t> &records, bool support_5g) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    cJSON *aps = cJSON_AddArrayToObject(root.get(), "aps");
    if (!aps || !cJSON_AddBoolToObject(root.get(), "support_5g", support_5g))
        return Json(nullptr, cJSON_Delete);
    for (const auto &record : records) {
        Json ap(cJSON_CreateObject(), cJSON_Delete);
        if (!cJSON_AddStringToObject(ap.get(), "ssid",
                                     reinterpret_cast<const char *>(record.ssid)) ||
            !cJSON_AddNumberToObject(ap.get(), "rssi", record.rssi) ||
            !cJSON_AddNumberToObject(ap.get(), "authmode", record.authmode) ||
            !cJSON_AddItemToArray(aps, ap.get()))
            return Json(nullptr, cJSON_Delete);
        ap.release();
    }
    return root;
}
} // namespace wifi_portal

#endif /* MYBOT_WIFI_PORTAL_JSON_H_ */
