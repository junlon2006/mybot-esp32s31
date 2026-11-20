/* SPDX-License-Identifier: MIT */
#include "ssid_manager.h"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
std::map<std::string, std::string> legacy = {{"ssid", "first"}, {"password", "old-key"}};
std::vector<uint8_t> disk, staged;
bool fail_write, fail_commit;
} // namespace
esp_err_t nvs_open(const char *, int, nvs_handle_t *handle) {
    *handle = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t) {
    staged.clear();
}
esp_err_t nvs_get_str(nvs_handle_t, const char *key, char *value, size_t *size) {
    auto it = legacy.find(key);
    if (it == legacy.end())
        return ESP_ERR_NVS_NOT_FOUND;
    assert(*size > it->second.size());
    std::memcpy(value, it->second.c_str(), it->second.size() + 1);
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *data, size_t *size) {
    if (disk.empty())
        return ESP_ERR_NVS_NOT_FOUND;
    if (data) {
        assert(*size == disk.size());
        std::memcpy(data, disk.data(), *size);
    }
    *size = disk.size();
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *value, size_t size) {
    if (fail_write)
        return ESP_ERR_INVALID_STATE;
    auto data = static_cast<const uint8_t *>(value);
    staged.assign(data, data + size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) {
    if (fail_commit)
        return ESP_ERR_INVALID_STATE;
    disk = staged;
    return ESP_OK;
}
int main() {
    auto &store = SsidManager::GetInstance();
    assert(store.GetSsidList().at(0).password == "old-key");
    fail_write = true;
    assert(!store.AddSsid("second", "new-key") && store.GetSsidList().size() == 1);
    fail_write = false;
    fail_commit = true;
    assert(!store.AddSsid("first", "changed") && store.GetSsidList()[0].password == "old-key");
    fail_commit = false;
    assert(store.AddSsid("second", "new-key") && store.GetSsidList()[0].ssid == "second");
    assert(store.SetDefaultSsid(1) && store.GetSsidList()[0].ssid == "first");
    /* After migration, reloading a failed update uses the complete blob over the legacy keys. */
    fail_commit = true;
    assert(!store.RemoveSsid(0) && store.GetSsidList().size() == 2);
    fail_commit = false;
    assert(store.RemoveSsid(0) && store.GetSsidList()[0].ssid == "second");
    assert(store.Clear() && store.GetSsidList().empty());
    fail_write = true;
    assert(!store.AddSsid("x", "") && store.GetSsidList().empty());
}
