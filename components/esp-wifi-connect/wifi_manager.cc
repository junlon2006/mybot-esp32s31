/* SPDX-License-Identifier: MIT */
/*
 * WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "wifi_station.h"
#include "wifi_configuration_ap.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#define TAG "WifiManager"

WifiManager &WifiManager::GetInstance() {
    static WifiManager instance;
    return instance;
}

WifiManager::WifiManager() = default;

WifiManager::~WifiManager() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event_callback_ = {};
    }
    StopStationLocked();
    StopConfigApLocked();
    // Destroy driver objects while both manager locks still exist. Their teardown
    // may join callbacks, so no state lock can be held here either.
    station_.reset();
    config_ap_.reset();
    if (initialized_) {
        esp_wifi_deinit();
    }
}

void WifiManager::NotifyEvent(WifiEvent event, const std::string &data) {
    // Event-loop callbacks must never wait for the lifecycle operation lock.
    std::function<void(WifiEvent, const std::string &)> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = event_callback_;
    }
    if (callback) {
        callback(event, data);
    }
}

void WifiManager::NotifyStationEvent(WifiEvent event, const std::string &data) {
    std::function<void(WifiEvent, const std::string &)> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!station_active_) {
            return;
        }
        if (event == WifiEvent::Connecting) {
            station_ssid_ = data;
        } else if (event == WifiEvent::Connected) {
            station_connected_ = true;
            station_ssid_ = data;
            // Called on the IP-event owner, after it has written the address.
            station_ip_address_ = station_->GetIpAddress();
        } else if (event == WifiEvent::Disconnected) {
            station_connected_ = false;
            station_ip_address_.clear();
        }
        callback = event_callback_;
    }
    if (callback) {
        callback(event, data);
    }
}

bool WifiManager::Initialize(const WifiManagerConfig &config) {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            ESP_LOGW(TAG, "Already initialized");
            return true;
        }
    }
    ESP_LOGI(TAG, "Initializing...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize netif
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Create event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return false;
    }

    auto station = std::make_unique<WifiStation>();
    auto config_ap = std::make_unique<WifiConfigurationAp>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        station_ = std::move(station);
        config_ap_ = std::move(config_ap);
        initialized_ = true;
    }
    ESP_LOGI(TAG, "Initialized");
    return true;
}

bool WifiManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

// ==================== Station Mode ====================

void WifiManager::StartStation() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            ESP_LOGE(TAG, "Not initialized");
            return;
        }
        if (station_active_) {
            ESP_LOGW(TAG, "Station already active");
            return;
        }
    }
    StopConfigApLocked();

    ESP_LOGI(TAG, "Starting station");

    // Apply configuration
    station_->SetScanIntervalRange(config_.station_scan_min_interval_seconds,
                                   config_.station_scan_max_interval_seconds);
    station_->SetFailureRetryCnt(config_.station_failure_retry_cnt);
    station_->SetHostname(config_.station_hostname);

    // Setup callbacks
    station_->OnScanBegin([this]() { NotifyStationEvent(WifiEvent::Scanning); });
    station_->OnConnect(
        [this](const std::string &ssid) { NotifyStationEvent(WifiEvent::Connecting, ssid); });
    station_->OnConnected(
        [this](const std::string &ssid) { NotifyStationEvent(WifiEvent::Connected, ssid); });
    station_->OnDisconnected([this](int reason) {
        NotifyStationEvent(WifiEvent::Disconnected, std::to_string(reason));
    });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        station_ssid_.clear();
        station_ip_address_.clear();
        station_connected_ = false;
        station_active_ = true;
    }
    station_->Start();
}

void WifiManager::StopStation() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    StopStationLocked();
}

void WifiManager::StopStationLocked() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!station_active_) {
            return;
        }
        // Revoke driver queries before unregister waits for in-flight callbacks.
        station_active_ = false;
        station_connected_ = false;
        station_ssid_.clear();
        station_ip_address_.clear();
    }

    ESP_LOGI(TAG, "Stopping station");
    station_->Stop();
    ESP_LOGI(TAG, "Station stopped");
    NotifyEvent(WifiEvent::Disconnected);
}

bool WifiManager::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_active_ && station_connected_;
}

std::string WifiManager::GetSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_active_ ? station_ssid_ : "";
}

std::string WifiManager::GetIpAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_active_ ? station_ip_address_ : "";
}

int WifiManager::GetRssi() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_connected_)
        return 0;
    return station_->GetRssi();
}

int WifiManager::GetChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_connected_)
        return 0;
    return station_->GetChannel();
}

std::string WifiManager::GetMacAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mac_address_.empty()) {
        return mac_address_;
    }

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
                 mac[4], mac[5]);
        mac_address_ = buf;
    }
    return mac_address_;
}

// ==================== Config AP Mode ====================

void WifiManager::StartConfigAp() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            ESP_LOGE(TAG, "Not initialized");
            return;
        }
        if (config_mode_active_) {
            ESP_LOGW(TAG, "Config AP already active");
            return;
        }
    }
    StopStationLocked();

    ESP_LOGI(TAG, "Starting config AP");

    config_ap_->SetSsid(config_.ap_ssid);
    config_ap_->SetSsidPrefix(config_.ssid_prefix);
    config_ap_->SetLanguage(config_.language);
    config_ap_->SetShowOtaConfig(config_.show_ota_config);
    config_ap_->SetShowSleepConfig(config_.show_sleep_config);

    // Web handler calls this when user submits config
    config_ap_->OnExitRequested([this]() {
        ESP_LOGI(TAG, "Config exit requested from web");
        StopConfigAp();
    });

    config_ap_->Start();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ap_ssid_ = config_ap_->GetSsid();
        ap_web_url_ = config_ap_->GetWebServerUrl();
        config_mode_active_ = true;
    }
    NotifyEvent(WifiEvent::ConfigModeEnter);
}

void WifiManager::StopConfigAp() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    StopConfigApLocked();
}

void WifiManager::StopConfigApLocked() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!config_mode_active_) {
            return;
        }
        config_mode_active_ = false;
        ap_ssid_.clear();
        ap_web_url_.clear();
    }

    ESP_LOGI(TAG, "Stopping config AP");
    config_ap_->Stop();
    NotifyEvent(WifiEvent::ConfigModeExit);
}

bool WifiManager::IsConfigMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_mode_active_;
}

std::string WifiManager::GetApSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_mode_active_ ? ap_ssid_ : "";
}

std::string WifiManager::GetApWebUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_mode_active_ ? ap_web_url_ : "";
}

// ==================== Power ====================

void WifiManager::SetPowerSaveLevel(WifiPowerSaveLevel level) {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!station_active_) {
            return;
        }
    }
    station_->SetPowerSaveLevel(level);
}

// ==================== Event ====================

void WifiManager::SetEventCallback(std::function<void(WifiEvent, const std::string &)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

WifiManagerConfig WifiManager::GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}
