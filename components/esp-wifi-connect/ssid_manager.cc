/* SPDX-License-Identifier: MIT */
#include "ssid_manager.h"
#include "wifi_credentials.h"

#include <algorithm>
#include <esp_log.h>
#include <nvs.h>

namespace {
constexpr char kNamespace[] = "wifi";
constexpr char kProfilesKey[] = "profiles_v1";
constexpr size_t kMaxProfiles = 10;
constexpr size_t kMaxBytes = 1 + kMaxProfiles * (2 + 32 + 64);

bool Decode(const std::vector<uint8_t> &bytes, std::vector<SsidItem> &profiles) {
    if (bytes.empty() || bytes[0] > kMaxProfiles)
        return false;
    size_t at = 1;
    for (unsigned i = 0; i < bytes[0]; ++i) {
        if (bytes.size() - at < 2)
            return false;
        size_t ssid_size = bytes[at++];
        size_t password_size = bytes[at++];
        if (bytes.size() - at < ssid_size + password_size)
            return false;
        const char *data = reinterpret_cast<const char *>(bytes.data() + at);
        SsidItem profile = {std::string(data, ssid_size),
                            std::string(data + ssid_size, password_size)};
        if (!wifi_credentials::Valid(profile.ssid, profile.password))
            return false;
        profiles.push_back(std::move(profile));
        at += ssid_size + password_size;
    }
    return at == bytes.size();
}

bool ReadLegacy(nvs_handle_t handle, std::vector<SsidItem> &profiles) {
    for (size_t i = 0; i < kMaxProfiles; ++i) {
        std::string suffix = i ? std::to_string(i) : "";
        char ssid[33], password[65];
        size_t size = sizeof(ssid);
        esp_err_t result = nvs_get_str(handle, ("ssid" + suffix).c_str(), ssid, &size);
        if (result == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (result != ESP_OK)
            return false;
        size = sizeof(password);
        result = nvs_get_str(handle, ("password" + suffix).c_str(), password, &size);
        if (result == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (result != ESP_OK)
            return false;
        if (wifi_credentials::Valid(ssid, password))
            profiles.push_back({ssid, password});
    }
    return true;
}
} // namespace

SsidManager::SsidManager() {
    LoadFromNvs();
}
SsidManager::~SsidManager() = default;

bool SsidManager::LoadFromNvs() {
    nvs_handle_t handle;
    esp_err_t result = nvs_open(kNamespace, NVS_READONLY, &handle);
    std::vector<SsidItem> profiles;
    bool ok = result == ESP_ERR_NVS_NOT_FOUND;
    if (result == ESP_OK) {
        size_t size = 0;
        result = nvs_get_blob(handle, kProfilesKey, nullptr, &size);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            ok = ReadLegacy(handle, profiles);
        } else if (result == ESP_OK && size > 0 && size <= kMaxBytes) {
            std::vector<uint8_t> bytes(size);
            ok = nvs_get_blob(handle, kProfilesKey, bytes.data(), &size) == ESP_OK &&
                 size == bytes.size() && Decode(bytes, profiles);
        }
        nvs_close(handle);
    }
    if (ok)
        ssid_list_.swap(profiles);
    loaded_ = ok;
    return ok;
}

bool SsidManager::SaveToNvs(std::vector<SsidItem> &profiles) {
    /* IDF does not make separate key writes a transaction. Keep the ordered list
     * in one versioned NVS value so a failed change cannot split an SSID/key pair. */
    std::vector<uint8_t> bytes{static_cast<uint8_t>(profiles.size())};
    for (const auto &profile : profiles) {
        bytes.push_back(static_cast<uint8_t>(profile.ssid.size()));
        bytes.push_back(static_cast<uint8_t>(profile.password.size()));
        bytes.insert(bytes.end(), profile.ssid.begin(), profile.ssid.end());
        bytes.insert(bytes.end(), profile.password.begin(), profile.password.end());
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, kProfilesKey, bytes.data(), bytes.size());
        if (result == ESP_OK)
            result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        ESP_LOGE("SsidManager", "Saving Wi-Fi profiles failed: %s", esp_err_to_name(result));
        loaded_ = false;
        return false;
    }
    ssid_list_.swap(profiles);
    loaded_ = true;
    return true;
}

std::vector<SsidItem> SsidManager::GetSsidList() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_)
        LoadFromNvs();
    return ssid_list_;
}

bool SsidManager::AddSsid(const std::string &ssid, const std::string &password) {
    if (!wifi_credentials::Valid(ssid, password))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_ && !LoadFromNvs())
        return false;
    auto profiles = ssid_list_;
    auto it = std::find_if(profiles.begin(), profiles.end(),
                           [&](const SsidItem &item) { return item.ssid == ssid; });
    if (it != profiles.end()) {
        it->password = password;
    } else {
        if (profiles.size() == kMaxProfiles)
            profiles.pop_back();
        profiles.insert(profiles.begin(), {ssid, password});
    }
    return SaveToNvs(profiles);
}

bool SsidManager::RemoveSsid(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if ((!loaded_ && !LoadFromNvs()) || index < 0 ||
        static_cast<size_t>(index) >= ssid_list_.size())
        return false;
    auto profiles = ssid_list_;
    profiles.erase(profiles.begin() + index);
    return SaveToNvs(profiles);
}

bool SsidManager::SetDefaultSsid(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if ((!loaded_ && !LoadFromNvs()) || index < 0 ||
        static_cast<size_t>(index) >= ssid_list_.size())
        return false;
    auto profiles = ssid_list_;
    std::rotate(profiles.begin(), profiles.begin() + index, profiles.begin() + index + 1);
    return SaveToNvs(profiles);
}

bool SsidManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SsidItem> empty;
    return SaveToNvs(empty);
}
