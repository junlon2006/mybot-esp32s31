/* SPDX-License-Identifier: MIT */
#include "wifi_configuration_ap.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace {
std::string body, reply, connected_ssid, connected_key;
size_t offset;
bool connect_ok, save_ok, send_ok;
unsigned saves, exits;
} // namespace
int httpd_req_recv(httpd_req_t *, char *dst, size_t capacity) {
    size_t count = std::min<size_t>(capacity, 5);
    std::memcpy(dst, body.data() + offset, count);
    offset += count;
    return count;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *) {
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *) {
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *) {
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *text) {
    reply = text;
    return send_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t httpd_resp_send_408(httpd_req_t *) {
    return ESP_FAIL;
}
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *) {
    return ESP_FAIL;
}
DnsServer::~DnsServer() = default;
WifiConfigurationAp::WifiConfigurationAp() = default;
WifiConfigurationAp::~WifiConfigurationAp() = default;
void WifiConfigurationAp::OnExitRequested(std::function<void()> callback) {
    on_exit_requested_ = std::move(callback);
}
bool WifiConfigurationAp::ConnectToWifi(const std::string &ssid, const std::string &key) {
    connected_ssid = ssid;
    connected_key = key;
    return connect_ok;
}
bool WifiConfigurationAp::Save(const std::string &, const std::string &) {
    ++saves;
    return save_ok;
}
int main() {
    WifiConfigurationAp ap;
    ap.OnExitRequested([] {
        assert(saves == 1 && reply == "{\"success\":true}");
        ++exits;
    });
    const std::string ssid(32, 'z'), key(64, '1');
    body = "{\"ssid\":\"" + ssid + "\",\"password\":\"" + key + "\"}";
    httpd_req_t req{body.size(), &ap};
    connect_ok = true;
    save_ok = true;
    for (bool response_ok : {true, false}) {
        send_ok = response_ok;
        offset = exits = saves = 0;
        reply.clear();
        assert(WifiConfigurationAp::HandleSubmit(&req) == (send_ok ? ESP_OK : ESP_FAIL));
        assert(exits == 1 && connected_ssid == ssid && connected_key == key);
    }
    send_ok = true;
    save_ok = false;
    offset = exits = saves = 0;
    assert(WifiConfigurationAp::HandleSubmit(&req) == ESP_OK);
    assert(saves == 1 && exits == 0 && reply.find("false") != std::string::npos);
    body = R"({"ssid":"prefix\u0000suffix","password":""})";
    req.content_len = body.size();
    offset = saves = 0;
    assert(WifiConfigurationAp::HandleSubmit(&req) == ESP_FAIL && !exits && !saves);
    body = R"({"ssid":"prefix\\u0000suffix","password":""})";
    req.content_len = body.size();
    connect_ok = false;
    offset = saves = 0;
    assert(WifiConfigurationAp::HandleSubmit(&req) == ESP_OK && !exits && !saves);
    assert(connected_ssid == R"(prefix\u0000suffix)");
}
