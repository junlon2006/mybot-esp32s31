/* SPDX-License-Identifier: MIT */
#include "../../components/esp-wifi-connect/wifi_credentials.h"
#include <cassert>
#include <string>

int main() {
    const std::string ssid(32, 's'), key(64, 'a');
    wifi_sta_config_t config{};
    assert(wifi_credentials::CopyToStation(config, ssid, key));
    assert(std::memcmp(config.ssid, ssid.data(), 32) == 0);
    assert(std::memcmp(config.password, key.data(), 64) == 0);
    assert(!wifi_credentials::CopyToStation(config, ssid + "s", key));
    assert(!wifi_credentials::CopyToStation(config, ssid, key + "b"));
    assert(!wifi_credentials::CopyToStation(config, "", key));
    assert(wifi_credentials::CopyToStation(config, "Cafe", ""));
    assert(config.ssid[4] == 0 && config.password[0] == 0 && config.password[63] == 0);
}
