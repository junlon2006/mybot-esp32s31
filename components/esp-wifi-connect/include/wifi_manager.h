/* SPDX-License-Identifier: MIT */
/*
 * WiFi Manager - Unified WiFi connection management
 *
 * Thread Safety:
 * - Lifecycle operations are synchronous and serialized separately from state queries.
 * - Callbacks run without the state lock, on the Wi-Fi event task or lifecycle caller.
 * - Callbacks may query state or replace the callback. Dispatch lifecycle requests to
 *   another task; never call Initialize/Start/Stop/SetPowerSaveLevel or wait for those
 *   operations in a callback.
 *
 * Usage:
 *   auto& wifi = WifiManager::GetInstance();
 *
 *   EventGroupHandle_t events = xEventGroupCreate();
 *   wifi.SetEventCallback([events](WifiEvent e, const std::string&) {
 *       if (e == WifiEvent::Connected) xEventGroupSetBits(events, BIT0);
 *       if (e == WifiEvent::ConfigModeExit) xEventGroupSetBits(events, BIT1);
 *   });
 *
 *   wifi.Initialize(config);
 *   wifi.StartStation();
 *   xEventGroupWaitBits(events, BIT0 | BIT1, pdTRUE, pdFALSE, portMAX_DELAY);
 */

#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <string>
#include <memory>
#include <functional>
#include <mutex>

#include "wifi_station.h"

class WifiStation;
class WifiConfigurationAp;

// WiFi events
enum class WifiEvent {
    Scanning,        // Started scanning for networks
    Connecting,      // Connecting to network (call GetSsid() for target)
    Connected,       // Successfully connected
    Disconnected,    // Disconnected from network
    ConfigModeEnter, // Entered config AP mode
    ConfigModeExit,  // Exited config AP mode
};

// Configuration
struct WifiManagerConfig {
    std::string ap_ssid;               // Optional complete AP SSID
    std::string ssid_prefix = "ESP32"; // AP mode SSID prefix
    std::string language = "zh-CN";    // Web UI language

    // Station mode scan interval with exponential backoff
    int station_scan_min_interval_seconds = 10;  // Initial scan interval (fast retry)
    int station_scan_max_interval_seconds = 300; // Maximum scan interval (5 minutes)
    std::string station_hostname;                // Optional DHCP hostname for station mode

    // How many times to retry the strongest same-SSID AP before falling back to
    // a weaker one (requires WIFI_ALL_CHANNEL_SCAN, which is the default when
    // remember_bssid is off). 0 = driver default (one attempt only).
    uint8_t station_failure_retry_cnt = 3;

    // Whether to show the OTA URL field in the config portal advanced tab.
    // Defaults to false so the field is hidden unless explicitly enabled.
    bool show_ota_config = false;

    // Whether to show the sleep mode toggle in the config portal advanced tab.
    bool show_sleep_config = false;
};

/**
 * WifiManager - Singleton for WiFi management
 */
class WifiManager {
  public:
    static WifiManager &GetInstance();

    // ==================== Lifecycle ====================

    bool Initialize(const WifiManagerConfig &config = WifiManagerConfig{});
    bool IsInitialized() const;

    // ==================== Station Mode ====================

    void StartStation(); // Synchronous setup; connection proceeds asynchronously
    void StopStation();  // Waits for in-flight event handlers and driver cleanup

    bool IsConnected() const;
    std::string GetSsid() const;
    std::string GetIpAddress() const;
    int GetRssi() const;
    int GetChannel() const;
    std::string GetMacAddress() const;

    // ==================== Config AP Mode ====================

    void StartConfigAp(); // Synchronously stops station and starts provisioning
    void StopConfigAp();  // Waits for the HTTP server, event handlers and driver cleanup

    bool IsConfigMode() const;
    std::string GetApSsid() const;
    std::string GetApWebUrl() const;

    // ==================== Power ====================

    void SetPowerSaveLevel(WifiPowerSaveLevel level);

    // ==================== Event ====================

    void SetEventCallback(std::function<void(WifiEvent, const std::string &)> callback);

    WifiManagerConfig GetConfig() const;

    WifiManager(const WifiManager &) = delete;
    WifiManager &operator=(const WifiManager &) = delete;

  private:
    WifiManager();
    ~WifiManager();

    void NotifyEvent(WifiEvent event, const std::string &data = "");
    void NotifyStationEvent(WifiEvent event, const std::string &data = "");
    // Only called while operation_mutex_ serializes the complete mode transition.
    void StopStationLocked();
    void StopConfigApLocked();

    WifiManagerConfig config_;
    std::unique_ptr<WifiStation> station_;
    std::unique_ptr<WifiConfigurationAp> config_ap_;

    // Driver start/stop may wait for event/HTTP tasks. Those tasks never take this lock.
    std::mutex operation_mutex_;
    // Short state/callback lock: never held across driver start/stop or a user callback.
    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool station_active_ = false;
    bool config_mode_active_ = false;
    bool station_connected_ = false;
    std::string station_ssid_;
    std::string station_ip_address_;
    std::string ap_ssid_;
    std::string ap_web_url_;

    std::function<void(WifiEvent, const std::string &)> event_callback_;
    mutable std::string mac_address_;
};

#endif // _WIFI_MANAGER_H_
