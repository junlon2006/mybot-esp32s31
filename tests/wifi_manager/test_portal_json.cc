/* SPDX-License-Identifier: MIT */
#include "../../components/esp-wifi-connect/wifi_portal_json.h"
#include <cassert>
#include <cstring>

static wifi_portal::Json round_trip(const wifi_portal::Json &json) {
    char *text = cJSON_PrintUnformatted(json.get());
    assert(text);
    wifi_portal::Json result(cJSON_Parse(text), cJSON_Delete);
    cJSON_free(text);
    assert(result);
    return result;
}

int main() {
    const char name[] = "Office \"A\"\\\n\t无线";
    wifi_ap_record_t record = {};
    std::memcpy(record.ssid, name, sizeof(name));
    record.rssi = -53;
    record.authmode = 3;
    auto scan = round_trip(wifi_portal::ScanList({record}, false));
    auto *ap = cJSON_GetArrayItem(cJSON_GetObjectItem(scan.get(), "aps"), 0);
    assert(!std::strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(ap, "ssid")), name));
    assert(cJSON_GetNumberValue(cJSON_GetObjectItem(ap, "rssi")) == -53);
    assert(cJSON_GetNumberValue(cJSON_GetObjectItem(ap, "authmode")) == 3);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(scan.get(), "support_5g")));
    auto saved = round_trip(wifi_portal::SavedList({{name, "not-in-output"}}));
    assert(cJSON_GetArraySize(saved.get()) == 1);
    assert(!std::strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(saved.get(), 0)), name));
}
