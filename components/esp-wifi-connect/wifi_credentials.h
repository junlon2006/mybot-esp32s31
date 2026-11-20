/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_WIFI_CREDENTIALS_H_
#define MYBOT_WIFI_CREDENTIALS_H_

#include <esp_wifi_types_generic.h>
#include <cstring>
#include <string_view>

namespace wifi_credentials {
inline bool Valid(std::string_view ssid, std::string_view password) {
    return !ssid.empty() && ssid.size() <= sizeof(wifi_sta_config_t::ssid) &&
           password.size() <= sizeof(wifi_sta_config_t::password) && ssid.find('\0') == ssid.npos &&
           password.find('\0') == password.npos;
}

/* IDF accepts full-width fields. Shorter values need zero padding. */
inline bool CopyToStation(wifi_sta_config_t &station, std::string_view ssid,
                          std::string_view password) {
    if (!Valid(ssid, password))
        return false;
    std::memset(station.ssid, 0, sizeof(station.ssid));
    std::memset(station.password, 0, sizeof(station.password));
    std::memcpy(station.ssid, ssid.data(), ssid.size());
    if (!password.empty())
        std::memcpy(station.password, password.data(), password.size());
    return true;
}
} // namespace wifi_credentials

#endif /* MYBOT_WIFI_CREDENTIALS_H_ */
