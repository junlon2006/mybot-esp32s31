/* SPDX-License-Identifier: MIT */
#include "wifi_configuration_ap.h"
#include "wifi_credentials.h"
#include <cJSON.h>
#include <memory>
#include <vector>

/* cJSON exposes strings as NUL-terminated. Reject escaped NULs before decoding
 * so a submitted credential cannot silently become its prefix. */
static bool HasEscapedNul(std::string_view body) {
    for (size_t i = 0; i + 1 < body.size(); ++i) {
        if (body[i] == '\\') {
            ++i;
            if (body.substr(i, 5) == "u0000")
                return true;
        }
    }
    return false;
}

esp_err_t WifiConfigurationAp::HandleSubmit(httpd_req_t *req) {
    if (!req->content_len || req->content_len > 1024)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");

    std::vector<char> body(req->content_len + 1, '\0');
    size_t read = 0;
    while (read < req->content_len) {
        const int received = httpd_req_recv(req, body.data() + read, req->content_len - read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
                httpd_resp_send_408(req);
            return ESP_FAIL;
        }
        read += received;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(
        cJSON_ParseWithLengthOpts(body.data(), body.size(), nullptr, true), cJSON_Delete);
    if (!json || std::memchr(body.data(), '\0', req->content_len) ||
        HasEscapedNul({body.data(), req->content_len}))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json.get(), "ssid"));
    auto *password_item = cJSON_GetObjectItemCaseSensitive(json.get(), "password");
    const char *password = password_item ? cJSON_GetStringValue(password_item) : "";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    if (!ssid || !password || !wifi_credentials::Valid(ssid, password))
        return httpd_resp_sendstr(req,
                                  "{\"success\":false,\"error\":\"Invalid Wi-Fi credentials\"}");

    auto *ap = static_cast<WifiConfigurationAp *>(req->user_ctx);
    if (!ap->ConnectToWifi(ssid, password))
        return httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Wi-Fi connection failed\"}");
    if (!ap->Save(ssid, password)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(
            req, "{\"success\":false,\"error\":\"Could not save Wi-Fi credentials\"}");
    }

    const esp_err_t sent = httpd_resp_sendstr(req, "{\"success\":true}");
    // The client may have already disconnected. The control task owns HTTP teardown.
    if (ap->on_exit_requested_)
        ap->on_exit_requested_();
    return sent;
}
