/* SPDX-License-Identifier: MIT */
#include "wifi_configuration_ap.h"
#include "wifi_manager.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

void Require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "assertion failed: %s\n", message);
        std::abort();
    }
}

enum class StopTarget { None, Station, Ap };
enum class StopWait { Gate, EventLoop };

struct Harness {
    std::mutex control;
    std::condition_variable changed;
    std::mutex event_loop;
    StopTarget target = StopTarget::None;
    StopWait wait = StopWait::Gate;
    bool stop_entered = false;
    bool event_loop_held = false;
    bool release_stop = false;
    bool block_station_start = false;
    bool start_entered = false;
    bool release_start = false;
    bool notification_entered = false;
    bool release_notification = false;
    std::atomic<int> driver_operations{0};
    std::atomic<bool> station_active{false};
    std::atomic<bool> ap_active{false};
    std::atomic<int> callbacks{0};
    std::atomic<int> power_calls{0};
    std::function<void()> scan_callback;
    std::function<void(const std::string &)> connecting_callback;
    std::function<void(const std::string &)> connected_callback;

    void Arm(StopTarget stop_target, StopWait stop_wait) {
        std::lock_guard<std::mutex> lock(control);
        target = stop_target;
        wait = stop_wait;
    }

    void BlockStop(StopTarget stop_target) {
        std::unique_lock<std::mutex> lock(control);
        if (target != stop_target)
            return;
        stop_entered = true;
        changed.notify_all();
        if (wait == StopWait::EventLoop) {
            lock.unlock();
            // IDF event-handler unregister takes the event loop's mutex.
            { std::lock_guard<std::mutex> loop_lock(event_loop); }
            lock.lock();
        } else {
            changed.wait(lock, [this] { return release_stop; });
        }
        target = StopTarget::None;
    }

    void WaitFor(bool Harness::*member) {
        std::unique_lock<std::mutex> lock(control);
        changed.wait(lock, [this, member] { return this->*member; });
    }

    void ReleaseStop() {
        std::lock_guard<std::mutex> lock(control);
        release_stop = true;
        changed.notify_all();
    }

    void BlockStationStart() {
        std::unique_lock<std::mutex> lock(control);
        if (!block_station_start)
            return;
        start_entered = true;
        changed.notify_all();
        changed.wait(lock, [this] { return release_start; });
    }
};

Harness harness;

class DriverOperation {
  public:
    DriverOperation() {
        Require(harness.driver_operations.fetch_add(1) == 0,
                "driver lifecycle operations must remain serialized");
    }
    ~DriverOperation() {
        harness.driver_operations.fetch_sub(1);
    }
};

WifiManager &Initialize() {
    auto &manager = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ap_ssid = "Regression AP";
    config.station_hostname = "regression-station";
    Require(manager.Initialize(config), "manager initializes");
    return manager;
}

void QueryManager(WifiManager &manager) {
    Require(manager.IsInitialized(), "manager remains initialized");
    (void)manager.IsConnected();
    (void)manager.GetSsid();
    (void)manager.GetIpAddress();
    (void)manager.GetRssi();
    (void)manager.GetChannel();
    (void)manager.IsConfigMode();
    (void)manager.GetApSsid();
    (void)manager.GetApWebUrl();
    Require(manager.GetMacAddress() == "00:01:02:03:04:05", "MAC query remains usable");
    Require(manager.GetConfig().station_hostname == "regression-station",
            "config remains published");
}

void InstallReentrantCallback(WifiManager &manager) {
    manager.SetEventCallback([&manager](WifiEvent, const std::string &) {
        QueryManager(manager);
        harness.callbacks.fetch_add(1);
        manager.SetEventCallback([&manager](WifiEvent, const std::string &) {
            QueryManager(manager);
            harness.callbacks.fetch_add(1);
        });
    });
}

void StationStopWithEvent(bool start_ap) {
    auto &manager = Initialize();
    manager.StartStation();
    InstallReentrantCallback(manager);
    harness.Arm(StopTarget::Station, StopWait::EventLoop);

    std::thread event([&manager] {
        std::lock_guard<std::mutex> event_lock(harness.event_loop);
        {
            std::lock_guard<std::mutex> lock(harness.control);
            harness.event_loop_held = true;
            harness.changed.notify_all();
        }
        harness.WaitFor(&Harness::stop_entered);
        // This callback was already dispatched before unregister began. The
        // manager may suppress its notification once shutdown is published.
        harness.scan_callback();
        QueryManager(manager);
        Require(!manager.IsConnected() && manager.GetSsid().empty() &&
                    manager.GetIpAddress().empty(),
                "station shutdown publishes inactive state before waiting for events");
        manager.SetEventCallback([&manager](WifiEvent, const std::string &) {
            QueryManager(manager);
            harness.callbacks.fetch_add(1);
            manager.SetEventCallback({});
        });
    });
    harness.WaitFor(&Harness::event_loop_held);
    std::thread operation([&manager, start_ap] {
        if (start_ap)
            manager.StartConfigAp();
        else
            manager.StopStation();
    });
    operation.join();
    event.join();
    Require(harness.callbacks.load() > 0, "shutdown delivers its lifecycle notification");
    Require(!harness.station_active.load(), "station stops");
    Require(harness.ap_active.load() == start_ap, "target AP state is correct");
    Require(manager.IsConfigMode() == start_ap, "published AP state is correct");
}

void ApStopWithQueries(bool start_station) {
    auto &manager = Initialize();
    manager.StartConfigAp();
    InstallReentrantCallback(manager);
    harness.Arm(StopTarget::Ap, StopWait::Gate);
    std::thread operation([&manager, start_station] {
        if (start_station)
            manager.StartStation();
        else
            manager.StopConfigAp();
    });
    harness.WaitFor(&Harness::stop_entered);
    QueryManager(manager);
    InstallReentrantCallback(manager);
    harness.ReleaseStop();
    operation.join();
    Require(harness.callbacks.load() > 0, "AP exit notification remains reentrant");
    Require(!harness.ap_active.load(), "AP stops");
    Require(harness.station_active.load() == start_station, "target station state is correct");
    Require(!manager.IsConfigMode(), "published AP state clears");
}

void ConnectedCallbackIsReentrant() {
    auto &manager = Initialize();
    InstallReentrantCallback(manager);
    manager.StartStation();
    harness.connecting_callback("Regression station");
    Require(manager.GetSsid() == "Regression station",
            "connecting event publishes the target SSID");
    harness.connected_callback("Regression station");
    Require(manager.IsConnected() && manager.GetSsid() == "Regression station" &&
                manager.GetIpAddress() == "192.0.2.2",
            "connected event publishes station status and address");
    Require(harness.callbacks.load() > 0, "station events reach the application");
    manager.StopStation();
    Require(!manager.IsConnected() && manager.GetSsid().empty() && manager.GetIpAddress().empty(),
            "station stop clears cached connection state");
    Require(harness.callbacks.load() > 1, "callback replacement takes effect");
}

void ConcurrentLifecycleIsSerialized() {
    auto &manager = Initialize();
    manager.StartStation();
    harness.Arm(StopTarget::Station, StopWait::Gate);
    std::thread switching([&manager] { manager.StartConfigAp(); });
    harness.WaitFor(&Harness::stop_entered);
    std::atomic<int> contenders_started{0};
    std::thread station([&] {
        contenders_started.fetch_add(1);
        manager.StartStation();
    });
    std::thread stop_ap([&] {
        contenders_started.fetch_add(1);
        manager.StopConfigAp();
    });
    std::thread stop_station([&] {
        contenders_started.fetch_add(1);
        manager.StopStation();
    });
    while (contenders_started.load() != 3)
        std::this_thread::yield();
    QueryManager(manager);
    std::this_thread::sleep_for(30ms);
    harness.ReleaseStop();
    switching.join();
    station.join();
    stop_ap.join();
    stop_station.join();
    Require(harness.driver_operations.load() == 0, "all driver operations finish");
    Require(!(harness.station_active.load() && harness.ap_active.load()),
            "concurrent transitions never leave both modes active");
}

void PowerSaveWaitsForStationStart() {
    auto &manager = Initialize();
    harness.block_station_start = true;
    std::thread starting([&manager] { manager.StartStation(); });
    harness.WaitFor(&Harness::start_entered);
    std::atomic<bool> power_started{false};
    std::thread power([&] {
        power_started.store(true);
        manager.SetPowerSaveLevel(WifiPowerSaveLevel::BALANCED);
    });
    while (!power_started.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(30ms);
    {
        std::lock_guard<std::mutex> lock(harness.control);
        harness.release_start = true;
        harness.changed.notify_all();
    }
    starting.join();
    power.join();
    Require(harness.power_calls.load() == 1, "power setting runs after the accepted station start");
}

void TransitionNotificationDoesNotSplitLifecycle() {
    auto &manager = Initialize();
    manager.StartStation();
    manager.SetEventCallback([](WifiEvent event, const std::string &) {
        if (event != WifiEvent::Disconnected)
            return;
        std::unique_lock<std::mutex> lock(harness.control);
        harness.notification_entered = true;
        harness.changed.notify_all();
        harness.changed.wait(lock, [] { return harness.release_notification; });
    });
    std::thread switching([&manager] { manager.StartConfigAp(); });
    harness.WaitFor(&Harness::notification_entered);
    std::atomic<bool> contender_started{false};
    std::thread competing([&] {
        contender_started.store(true);
        manager.StartStation();
    });
    while (!contender_started.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(30ms);
    {
        std::lock_guard<std::mutex> lock(harness.control);
        harness.release_notification = true;
        harness.changed.notify_all();
    }
    switching.join();
    competing.join();
    Require(harness.station_active.load() && !harness.ap_active.load(),
            "the later station start follows the complete AP transition");
    Require(!manager.IsConfigMode(), "manager state agrees with serialized transition order");
}

struct TestCase {
    const char *name;
    void (*run)();
};

bool RunIsolated(const TestCase &test) {
    const pid_t child = fork();
    Require(child >= 0, "fork succeeds");
    if (child == 0) {
        test.run();
        // Avoid involving the process-static singleton's teardown in the
        // per-case lifetime; the firmware manager itself is process lifetime.
        _exit(0);
    }
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    int status = 0;
    for (;;) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        Require(result >= 0, "waitpid succeeds");
        if (result == child)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            Require(waitpid(child, &status, 0) == child, "timed-out child is reaped");
            std::fprintf(stderr, "FAIL %s: three-second deadlock watchdog\n", test.name);
            return false;
        }
        std::this_thread::sleep_for(5ms);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "FAIL %s: child status %d\n", test.name, status);
        return false;
    }
    std::printf("PASS %s\n", test.name);
    std::fflush(stdout);
    return true;
}

} // namespace

// The actual station/AP class layouts and callback signatures come from the
// production headers. These methods model only the external driver boundary.
WifiStation::WifiStation() {
    ssid_ = "Regression station";
    ip_address_ = "192.0.2.2";
}
WifiStation::~WifiStation() = default;
void WifiStation::Start() {
    DriverOperation operation;
    harness.BlockStationStart();
    Require(!harness.ap_active.load(), "station cannot start while AP is active");
    harness.station_active.store(true);
}
void WifiStation::Stop() {
    DriverOperation operation;
    harness.BlockStop(StopTarget::Station);
    harness.station_active.store(false);
}
bool WifiStation::IsConnected() {
    return harness.station_active.load();
}
int8_t WifiStation::GetRssi() {
    return -40;
}
uint8_t WifiStation::GetChannel() {
    return 6;
}
void WifiStation::SetPowerSaveLevel(WifiPowerSaveLevel) {
    DriverOperation operation;
    Require(harness.station_active.load(), "power setting must wait for station startup");
    harness.power_calls.fetch_add(1);
}
void WifiStation::SetScanIntervalRange(int, int) {
}
void WifiStation::OnScanBegin(std::function<void()> callback) {
    on_scan_begin_ = callback;
    harness.scan_callback = std::move(callback);
}
void WifiStation::OnConnect(std::function<void(const std::string &)> callback) {
    on_connect_ = callback;
    harness.connecting_callback = std::move(callback);
}
void WifiStation::OnConnected(std::function<void(const std::string &)> callback) {
    on_connected_ = callback;
    harness.connected_callback = std::move(callback);
}
void WifiStation::OnDisconnected(std::function<void(int)> callback) {
    on_disconnected_ = std::move(callback);
}

DnsServer::DnsServer() = default;
DnsServer::~DnsServer() = default;
WifiConfigurationAp::WifiConfigurationAp() = default;
WifiConfigurationAp::~WifiConfigurationAp() = default;
void WifiConfigurationAp::SetSsid(const std::string &ssid) {
    ssid_ = ssid;
}
void WifiConfigurationAp::SetSsidPrefix(const std::string &prefix) {
    ssid_prefix_ = prefix;
}
void WifiConfigurationAp::SetLanguage(const std::string &language) {
    language_ = language;
}
void WifiConfigurationAp::OnExitRequested(std::function<void()> callback) {
    on_exit_requested_ = std::move(callback);
}
void WifiConfigurationAp::Start() {
    DriverOperation operation;
    Require(!harness.station_active.load(), "AP cannot start while station is active");
    harness.ap_active.store(true);
}
void WifiConfigurationAp::Stop() {
    DriverOperation operation;
    harness.BlockStop(StopTarget::Ap);
    harness.ap_active.store(false);
}
std::string WifiConfigurationAp::GetSsid() {
    return ssid_;
}
std::string WifiConfigurationAp::GetWebServerUrl() {
    return "http://192.0.2.1";
}

int main() {
    const TestCase tests[] = {
        {"station stop drains in-flight event", [] { StationStopWithEvent(false); }},
        {"station-to-AP drains in-flight event", [] { StationStopWithEvent(true); }},
        {"AP stop permits state and callback access", [] { ApStopWithQueries(false); }},
        {"AP-to-station permits state and callback access", [] { ApStopWithQueries(true); }},
        {"station callback getters and replacement", ConnectedCallbackIsReentrant},
        {"concurrent lifecycle stays serialized", ConcurrentLifecycleIsSerialized},
        {"power save waits for station start", PowerSaveWaitsForStationStart},
        {"transition notification preserves lifecycle order",
         TransitionNotificationDoesNotSplitLifecycle},
    };
    bool passed = true;
    for (const auto &test : tests)
        passed = RunIsolated(test) && passed;
    return passed ? 0 : 1;
}
