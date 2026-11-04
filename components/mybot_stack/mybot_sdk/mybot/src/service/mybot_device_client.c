/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_device_client.h"
#include <mybot/mybot_build_config.h>
#include "mybot_http_client.h"
#include "mybot_json.h"

#include <api/aosl_log.h>
#include <hal/aosl_hal_memory.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool url_unreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~';
}

static int encode_path_segment(const char *value, char *encoded, size_t encoded_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t offset = 0;
    if (!value || !value[0] || !encoded || encoded_size == 0) {
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        size_t needed = url_unreserved(*p) ? 1U : 3U;
        if (needed >= encoded_size - offset) {
            return -1;
        }
        if (needed == 1) {
            encoded[offset++] = (char)*p;
        } else {
            encoded[offset++] = '%';
            encoded[offset++] = hex[*p >> 4];
            encoded[offset++] = hex[*p & 0x0f];
        }
    }
    encoded[offset] = '\0';
    return 0;
}

static int build_device_url(const char *base_url, const char *device_id, const char *suffix,
                            char *url, size_t url_size) {
    char encoded_id[MYBOT_DEVICE_CLIENT_MAX_ID * 3];
    if (!base_url || !device_id || !device_id[0] || !suffix ||
        strlen(device_id) >= MYBOT_DEVICE_CLIENT_MAX_ID ||
        encode_path_segment(device_id, encoded_id, sizeof(encoded_id)) < 0) {
        return -1;
    }
    int written = snprintf(url, url_size, "%s/devices/%s%s", base_url, encoded_id, suffix);
    return written >= 0 && (size_t)written < url_size ? 0 : -1;
}

static int build_authorization_header(const char *scheme, const char *credential, char *header,
                                      size_t header_size) {
    if (!scheme || !mybot_http_header_value_is_safe(credential, false)) {
        return -1;
    }
    int written = snprintf(header, header_size, "Authorization: %s%s\r\n", scheme, credential);
    return written >= 0 && (size_t)written < header_size ? 0 : -1;
}

/* Own only the temporary header; URL/body are borrowed and raw belongs to the caller. */
static int post_device_json(const char *url, const char *device_token, const char *body,
                            int timeout_ms, mybot_http_client_response_t *raw,
                            bool *request_attempted) {
    if (request_attempted) {
        *request_attempted = false;
    }
    size_t header_size = MYBOT_DEVICE_CLIENT_MAX_TOKEN + 32U;
    char *headers = (char *)aosl_hal_malloc(header_size);
    if (!headers) {
        return -1;
    }
    int ret = build_authorization_header("Device ", device_token, headers, header_size);
    if (ret == 0) {
        if (request_attempted) {
            *request_attempted = true;
        }
        ret = timeout_ms > 0
                  ? mybot_http_client_post_timeout_ex(url, "application/json", body, headers,
                                                      timeout_ms, raw)
                  : mybot_http_client_post_ex(url, "application/json", body, headers, raw);
    }
    aosl_hal_free(headers);
    return ret;
}

static int copy_optional_json_string(const mybot_json_t *object, const char *name,
                                     char *destination, size_t destination_size) {
    mybot_json_t *item = mybot_json_get_object_item(object, name);
    if (!item || item->type == MYBOT_JSON_NULL) {
        return 0;
    }
    const char *value = mybot_json_get_string(item);
    if (!value) {
        return -1;
    }
    if (!destination || destination_size == 0 || strlen(value) >= destination_size) {
        return -1;
    }
    memcpy(destination, value, strlen(value) + 1);
    return 0;
}

static int copy_required_json_string(const mybot_json_t *object, const char *name,
                                     char *destination, size_t destination_size) {
    const char *value = mybot_json_get_string(mybot_json_get_object_item(object, name));
    if (!value || !value[0] || !destination || destination_size == 0) {
        return -1;
    }

    size_t len = strlen(value);
    if (len >= destination_size) {
        return -1;
    }
    memcpy(destination, value, len + 1);
    return 0;
}

static void copy_json_integer(const mybot_json_t *object, const char *name, int *destination) {
    int64_t value;
    if (mybot_json_get_integer(mybot_json_get_object_item(object, name), &value)) {
        if (value > INT_MAX) {
            *destination = INT_MAX;
        } else if (value < INT_MIN) {
            *destination = INT_MIN;
        } else {
            *destination = (int)value;
        }
    }
}

static char *build_pair_code_body(const char *device_id, const char *firmware_ver,
                                  const char *hw_model) {
    mybot_json_t *root = mybot_json_create_object();
    if (!root || mybot_json_add_string(root, "device_id", device_id) < 0 ||
        (firmware_ver && firmware_ver[0] &&
         mybot_json_add_string(root, "firmware_version", firmware_ver) < 0) ||
        (hw_model && hw_model[0] && mybot_json_add_string(root, "hardware_model", hw_model) < 0)) {
        mybot_json_delete(root);
        return NULL;
    }

    char *body = mybot_json_print_unformatted(root);
    mybot_json_delete(root);
    return body;
}

static char *build_conversation_start_body(mybot_translation_mode_t mode,
                                           const char *source_language,
                                           const char *target_language) {
    mybot_json_t *root = mybot_json_create_object();
    mybot_json_t *audio = NULL;
    mybot_json_t *features = NULL;
    mybot_json_t *translation = NULL;
    char *body = NULL;

    if (!root || mybot_json_add_string(root, "trigger", "button") < 0) {
        goto done;
    }

    audio = mybot_json_create_object();
    if (!audio || mybot_json_add_number(audio, "p_time", MYBOT_AUDIO_PTIME_MS) < 0 ||
        mybot_json_add_string(audio, "codec", "G722") < 0) {
        goto done;
    }
    int add_result = mybot_json_add_item(root, "audio", audio);
    if (add_result < 0) {
        goto done;
    }
    audio = NULL;

    features = mybot_json_create_object();
    if (!features) {
        goto done;
    }
#if MYBOT_CLOUD_AEC
    if (mybot_json_add_bool(features, "cloud_aec", true) < 0) {
#else
    if (mybot_json_add_bool(features, "cloud_aec", false) < 0) {
#endif
        goto done;
    }
#if MYBOT_AI_QOS
    if (mybot_json_add_bool(features, "ai_qos", true) < 0 ||
        mybot_json_add_number(features, "fast_send_multiplier", MYBOT_FAST_SEND_MULTIPLIER) < 0) {
#else
    if (mybot_json_add_bool(features, "ai_qos", false) < 0) {
#endif
        goto done;
    }
    add_result = mybot_json_add_item(root, "features", features);
    if (add_result < 0) {
        goto done;
    }
    features = NULL;

    translation = mybot_json_create_object();
    bool bidirectional = mode == MYBOT_TRANSLATION_LISTEN_SPEAK;
    if (!translation ||
        mybot_json_add_string(translation, "mode", bidirectional ? "listen_speak" : "listen") < 0 ||
        mybot_json_add_string(translation, "remote_language",
                              bidirectional ? target_language : source_language) < 0 ||
        mybot_json_add_string(translation, "local_language",
                              bidirectional ? source_language : target_language) < 0 ||
        mybot_json_add_item(root, "translation", translation) < 0) {
        goto done;
    }
    translation = NULL;

    body = mybot_json_print_unformatted(root);

done:
    mybot_json_delete(translation);
    mybot_json_delete(features);
    mybot_json_delete(audio);
    mybot_json_delete(root);
    return body;
}

static char *build_stop_conversation_body(const char *conversation_id, const char *reason) {
    mybot_json_t *root = mybot_json_create_object();
    if (!root || mybot_json_add_string(root, "conversation_id", conversation_id) < 0 ||
        (reason && mybot_json_add_string(root, "reason", reason) < 0)) {
        mybot_json_delete(root);
        return NULL;
    }

    char *body = mybot_json_print_unformatted(root);
    mybot_json_delete(root);
    return body;
}

static bool json_string_is(const mybot_json_t *object, const char *name, const char *expected) {
    const char *value = mybot_json_get_string(mybot_json_get_object_item(object, name));
    return value && strcmp(value, expected) == 0;
}

static bool language_is_valid(const char *language) {
    if (!language || !language[0] || strlen(language) > 32) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)language; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-')) {
            return false;
        }
    }
    return true;
}

static bool account_is_valid(const char *account) {
    if (!account || !account[0]) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)account; *p; p++) {
        if (*p <= ' ' || *p >= 127) {
            return false;
        }
    }
    return true;
}

static int decimal_digits(const char *text, size_t count) {
    int value = 0;
    for (size_t i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10 + text[i] - '0';
    }
    return value;
}

/* Parse the service's UTC ISO 8601 timestamps without depending on local TZ or timegm(). */
static int parse_utc_expiry(const char *text, int64_t *epoch) {
    static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!text || strlen(text) < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') {
        return -1;
    }
    int year = decimal_digits(text, 4), month = decimal_digits(text + 5, 2);
    int day = decimal_digits(text + 8, 2), hour = decimal_digits(text + 11, 2);
    int minute = decimal_digits(text + 14, 2), second = decimal_digits(text + 17, 2);
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < 1970 || month < 1 || month > 12 || day < 1 ||
        day > month_days[month - 1] + (month == 2 && leap ? 1 : 0) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return -1;
    }
    const char *suffix = text + 19;
    if (*suffix == '.') {
        suffix++;
        const char *fraction = suffix;
        while (*suffix >= '0' && *suffix <= '9') {
            suffix++;
        }
        if (suffix == fraction) {
            return -1;
        }
    }
    if (strcmp(suffix, "Z") != 0 && strcmp(suffix, "+00:00") != 0) {
        return -1;
    }
    int64_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 366 : 365;
    }
    for (int m = 1; m < month; m++) {
        days += month_days[m - 1] + (m == 2 && leap ? 1 : 0);
    }
    *epoch = ((days + day - 1) * 24 + hour) * 3600 + minute * 60 + second;
    return 0;
}

static void set_start_error(mybot_device_conversation_t *resp, const char *code) {
    snprintf(resp->error_code, sizeof(resp->error_code), "%s", code);
}

static int reject_translation_route(mybot_device_conversation_t *resp, const char *code,
                                    const char *reason) {
    set_start_error(resp, code);
    AOSL_LOG_ERR("translation route rejected: code=%s reason=%s", code, reason);
    return -1;
}

static int parse_translation_route(const mybot_json_t *data, mybot_translation_mode_t mode,
                                   const char *source_language, const char *target_language,
                                   mybot_device_conversation_t *resp) {
    mybot_json_t *session = mybot_json_get_object_item(data, "translation_session");
    mybot_json_t *routes = mybot_json_get_object_item(session, "routes");
    bool bidirectional = mode == MYBOT_TRANSLATION_LISTEN_SPEAK;
    if (!json_string_is(data, "voice_agent_type", "v2vt") ||
        !json_string_is(data, "data_channel", "rtm") ||
        !json_string_is(data, "status", "started") ||
        !json_string_is(session, "mode", bidirectional ? "listen_speak" : "listen") || !routes ||
        routes->type != MYBOT_JSON_ARRAY || !routes->child) {
        return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE", "session_contract");
    }
    const char *session_expiry =
        mybot_json_get_string(mybot_json_get_object_item(data, "expires_at"));
    if (parse_utc_expiry(session_expiry, &resp->expires_at) < 0)
        return reject_translation_route(resp, "INVALID_TRANSLATION_EXPIRY", "utc_timestamp");
    unsigned seen = 0;
    for (const mybot_json_t *route = routes->child; route; route = route->next) {
        bool listen = json_string_is(route, "direction", "listen");
        bool speak = json_string_is(route, "direction", "speak");
        unsigned index = listen ? 0 : 1;
        if (route->type != MYBOT_JSON_OBJECT || (!listen && !speak) || (speak && !bidirectional) ||
            (seen & (1U << index)))
            return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE", "route_direction");
        seen |= 1U << index;
        mybot_device_translation_route_t *out = &resp->routes[index];
        out->direction =
            listen ? MYBOT_TRANSLATION_DIRECTION_LISTEN : MYBOT_TRANSLATION_DIRECTION_SPEAK;
        const mybot_json_t *rtc = mybot_json_get_object_item(route, "rtc");
        const char *expected_source = listen && bidirectional ? target_language : source_language;
        const char *expected_target = listen && bidirectional ? source_language : target_language;
        if (!json_string_is(route, "input_role", listen ? "remote_original" : "local_original") ||
            !json_string_is(route, "output_role",
                            listen ? "local_translation" : "remote_translation") ||
            copy_required_json_string(rtc, "app_id", out->rtc_app_id, sizeof(out->rtc_app_id)) <
                0 ||
            copy_required_json_string(rtc, "channel", out->rtc_channel, sizeof(out->rtc_channel)) <
                0 ||
            copy_required_json_string(rtc, "uid", out->rtc_uid, sizeof(out->rtc_uid)) < 0 ||
            copy_required_json_string(rtc, "token", out->rtc_token, sizeof(out->rtc_token)) < 0 ||
            copy_required_json_string(route, "agent_uid", out->rtc_agent_uid,
                                      sizeof(out->rtc_agent_uid)) < 0 ||
            copy_required_json_string(route, "source_language", out->source_language,
                                      sizeof(out->source_language)) < 0 ||
            copy_required_json_string(route, "target_language", out->target_language,
                                      sizeof(out->target_language)) < 0 ||
            !account_is_valid(out->rtc_app_id) ||
            !mybot_http_header_value_is_safe(out->rtc_channel, false) ||
            !mybot_http_header_value_is_safe(out->rtc_token, false) ||
            !account_is_valid(out->rtc_uid) || !account_is_valid(out->rtc_agent_uid) ||
            strcmp(out->rtc_uid, out->rtc_agent_uid) == 0 ||
            strcmp(out->source_language, expected_source) ||
            strcmp(out->target_language, expected_target)) {
            return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE",
                                            "roles_credentials_or_languages");
        }
        const mybot_json_t *tts = mybot_json_get_object_item(route, "tts_enabled");
        if (!tts || (tts->type != MYBOT_JSON_TRUE && tts->type != MYBOT_JSON_FALSE))
            return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE", "tts_type");
        out->tts_enabled = tts->type == MYBOT_JSON_TRUE;
        if (speak && !out->tts_enabled)
            return reject_translation_route(resp, "TRANSLATION_TTS_DISABLED", "speak_tts_disabled");
        const mybot_json_t *publisher = mybot_json_get_object_item(route, "audio_publisher_uid");
        if (out->tts_enabled) {
            if (copy_required_json_string(route, "audio_publisher_uid", out->audio_publisher_uid,
                                          sizeof(out->audio_publisher_uid)) < 0 ||
                !account_is_valid(out->audio_publisher_uid) ||
                strcmp(out->audio_publisher_uid, out->rtc_agent_uid))
                return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE",
                                                "audio_publisher");
        } else if (!publisher || publisher->type != MYBOT_JSON_NULL) {
            return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE",
                                            "disabled_publisher");
        }
        const char *route_expiry =
            mybot_json_get_string(mybot_json_get_object_item(rtc, "expires_at"));
        if (parse_utc_expiry(route_expiry, &out->expires_at) < 0 ||
            out->expires_at != resp->expires_at)
            return reject_translation_route(resp, "INVALID_TRANSLATION_EXPIRY", "route_expiry");
    }
    if (seen != (bidirectional ? 3U : 1U))
        return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE", "missing_route");
    if (bidirectional) {
        const mybot_device_translation_route_t *listen = &resp->routes[0];
        const mybot_device_translation_route_t *speak = &resp->routes[1];
        if (strcmp(listen->rtc_app_id, speak->rtc_app_id) ||
            strcmp(listen->rtc_channel, speak->rtc_channel))
            return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE",
                                            "project_or_channel");
        const char *identities[] = {listen->rtc_uid, listen->rtc_agent_uid, speak->rtc_uid,
                                    speak->rtc_agent_uid};
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = i + 1; j < 4; ++j)
                if (!strcmp(identities[i], identities[j]))
                    return reject_translation_route(resp, "INVALID_TRANSLATION_ROUTE",
                                                    "identity_collision");
    }
    time_t now = time(NULL);
    if (now < 1577836800)
        return reject_translation_route(resp, "TIME_NOT_SYNCED", "local_utc_unavailable");
    if (resp->expires_at <= (int64_t)now)
        return reject_translation_route(resp, "TRANSLATION_EXPIRED", "token_expired");
    resp->mode = mode;
    resp->route_count = bidirectional ? 2 : 1;
    return 0;
}

/* ----------------------------------------------------------
 * Device service HTTP client
 * ---------------------------------------------------------- */

/* True if the HTTP response status is a 2xx success. */
static bool http_response_ok(const mybot_http_client_response_t *resp) {
    return resp->status_code >= 200 && resp->status_code < 300;
}

/* Return borrowed data; the caller owns *root even when data is missing. */
static mybot_json_t *parse_response_data(const mybot_http_client_response_t *raw,
                                         mybot_json_t **root) {
    *root = raw->body ? mybot_json_parse(raw->body) : NULL;
    return mybot_json_get_object_item(*root, "data");
}

int mybot_device_client_create_pair_code(const char *base_url, const char *device_id,
                                         const char *firmware_ver, const char *hw_model,
                                         mybot_device_pair_code_t *resp) {
    if (!base_url || !device_id || !resp) {
        return -1;
    }
    memset(resp, 0, sizeof(*resp));

    char *body = build_pair_code_body(device_id, firmware_ver, hw_model);
    if (!body) {
        return -1;
    }

    char url[MYBOT_DEVICE_CLIENT_MAX_URL];
    snprintf(url, sizeof(url), "%s/devices/pair-codes", base_url);

    AOSL_LOG_NTC("POST %s", url);

    mybot_http_client_response_t raw;
    memset(&raw, 0, sizeof(raw));
    mybot_json_t *root = NULL;
    int result = -1;

    int ret = mybot_http_client_post_ex(url, "application/json", body, NULL, &raw);
    mybot_json_free_string(body);
    if (ret < 0) {
        AOSL_LOG_ERR("POST %s failed (http)", url);
        goto done;
    }

    AOSL_LOG_NTC("POST %s -> status=%d", url, raw.status_code);

    if (!http_response_ok(&raw)) {
        int status = raw.status_code;
        AOSL_LOG_ERR("POST %s -> HTTP error %d", url, raw.status_code);
        result = status > 0 ? status : -1;
        goto done;
    }

    mybot_json_t *data = parse_response_data(&raw, &root);
    if (!data) {
        goto done;
    }

    int code_result = copy_required_json_string(data, "code", resp->code, sizeof(resp->code));
    int pair_token_result =
        copy_required_json_string(data, "pair_token", resp->pair_token, sizeof(resp->pair_token));
    if (code_result < 0 || pair_token_result < 0) {
        AOSL_LOG_ERR("pair-code response missing or invalid code/pair_token");
        goto done;
    }
    copy_json_integer(data, "poll_after_seconds", &resp->poll_after_seconds);

    AOSL_LOG_NTC("pair_code: code=%s poll=%ds", resp->code, resp->poll_after_seconds);
    result = 0;

done:
    mybot_json_delete(root);
    mybot_http_client_response_free(&raw);
    return result;
}

int mybot_device_client_get_binding_status(const char *base_url, const char *device_id,
                                           const char *auth_header, mybot_device_binding_t *resp) {
    if (!base_url || !device_id || !auth_header || !resp) {
        return -1;
    }
    memset(resp, 0, sizeof(*resp));

    char url[MYBOT_DEVICE_CLIENT_MAX_URL];
    mybot_http_client_response_t raw;
    memset(&raw, 0, sizeof(raw));
    mybot_json_t *root = NULL;
    int result = -1;
    char *extra_hdrs = (char *)aosl_hal_malloc(MYBOT_DEVICE_CLIENT_MAX_TOKEN + 32U);
    if (!extra_hdrs) {
        goto done;
    }
    if (build_device_url(base_url, device_id, "/binding-status", url, sizeof(url)) < 0) {
        goto done;
    }

    if (build_authorization_header("", auth_header, extra_hdrs,
                                   MYBOT_DEVICE_CLIENT_MAX_TOKEN + 32U) < 0) {
        goto done;
    }

    AOSL_LOG_NTC("GET %s", url);

    if (mybot_http_client_get_ex(url, extra_hdrs, &raw) < 0) {
        AOSL_LOG_ERR("GET %s failed (http)", url);
        goto done;
    }

    AOSL_LOG_NTC("GET %s -> status=%d", url, raw.status_code);

    if (!http_response_ok(&raw)) {
        int status = raw.status_code;
        AOSL_LOG_ERR("GET %s -> HTTP error %d", url, raw.status_code);
        result = status > 0 ? status : -1;
        goto done;
    }

    mybot_json_t *data = parse_response_data(&raw, &root);
    if (!data) {
        goto done;
    }

    if (copy_optional_json_string(data, "status", resp->status, sizeof(resp->status)) < 0 ||
        copy_optional_json_string(data, "device_token", resp->device_token,
                                  sizeof(resp->device_token)) < 0 ||
        copy_optional_json_string(data, "agent_id", resp->agent_id, sizeof(resp->agent_id)) < 0 ||
        copy_optional_json_string(data, "agent_name", resp->agent_name, sizeof(resp->agent_name)) <
            0) {
        goto done;
    }
    copy_json_integer(data, "poll_after_seconds", &resp->poll_after_seconds);

    AOSL_LOG_DBG("binding: status=%s agent=%s has_token=%d poll=%ds", resp->status,
                 resp->agent_name, resp->device_token[0] ? 1 : 0, resp->poll_after_seconds);
    result = 0;

done:
    mybot_json_delete(root);
    mybot_http_client_response_free(&raw);
    aosl_hal_free(extra_hdrs);
    return result;
}

int mybot_device_client_start_conversation(const char *base_url, const char *device_id,
                                           const char *device_token, mybot_translation_mode_t mode,
                                           const char *source_language, const char *target_language,
                                           mybot_device_conversation_t *resp) {
    if (!resp) {
        return -1;
    }
    memset(resp, 0, sizeof(*resp));
    if (mode != MYBOT_TRANSLATION_LISTEN && mode != MYBOT_TRANSLATION_LISTEN_SPEAK) {
        set_start_error(resp, "INVALID_TRANSLATION_MODE");
        return -1;
    }
    if (!base_url || !device_id || !device_token || !language_is_valid(source_language) ||
        !language_is_valid(target_language) || strcmp(source_language, target_language) == 0) {
        set_start_error(resp, "INVALID_TRANSLATION_LANGUAGES");
        return -1;
    }
    char url[MYBOT_DEVICE_CLIENT_MAX_URL];
    if (build_device_url(base_url, device_id, "/conversations/start", url, sizeof(url)) < 0 ||
        !mybot_http_header_value_is_safe(device_token, false)) {
        set_start_error(resp, "INVALID_DEVICE_REQUEST");
        return -1;
    }
    /* Do not create a cloud session until its credential expiry can be checked locally. */
    if (time(NULL) < 1577836800) {
        set_start_error(resp, "TIME_NOT_SYNCED");
        AOSL_LOG_ERR("translation start deferred: local UTC clock not synchronized");
        return -1;
    }
    char *body = build_conversation_start_body(mode, source_language, target_language);
    if (!body) {
        set_start_error(resp, "OUT_OF_MEMORY");
        return -1;
    }
    mybot_http_client_response_t raw = {0};
    mybot_json_t *root = NULL;
    int result = -1;
    bool request_attempted = false;
    /* Provisioning the translation agent can outlast the normal 5 s HTTP budget. */
    int ret = post_device_json(url, device_token, body, 30000, &raw, &request_attempted);
    mybot_json_free_string(body);
    root = raw.body ? mybot_json_parse(raw.body) : NULL;
    mybot_json_t *data = mybot_json_get_object_item(root, "data");
    mybot_json_t *error = mybot_json_get_object_item(root, "error");
    mybot_json_t *details = mybot_json_get_object_item(error, "details");
    mybot_json_t *cleanup = mybot_json_get_object_item(details, "cleanup_required");

    /* Preserve the parent's identity before any status or media validation. */
    if (copy_required_json_string(data, "conversation_id", resp->conversation_id,
                                  sizeof(resp->conversation_id)) < 0) {
        (void)copy_required_json_string(details, "conversation_id", resp->conversation_id,
                                        sizeof(resp->conversation_id));
    }
    resp->cleanup_required =
        resp->conversation_id[0] != '\0' || (cleanup && cleanup->type == MYBOT_JSON_TRUE);
    (void)copy_optional_json_string(error, "code", resp->error_code, sizeof(resp->error_code));
    if (ret < 0) {
        bool rejected = raw.status_code >= 400 || (cleanup && cleanup->type == MYBOT_JSON_FALSE);
        resp->start_unknown = request_attempted && !resp->conversation_id[0] && !rejected;
        if (!resp->error_code[0]) {
            set_start_error(resp, !request_attempted    ? "OUT_OF_MEMORY"
                                  : resp->start_unknown ? "START_RESULT_UNKNOWN"
                                                        : "CONVERSATION_START_FAILED");
        }
        goto done;
    }
    if (raw.status_code != 201) {
        /* A definite rejection is known, unless the server explicitly requires
         * cleanup but failed to supply a usable parent identity. */
        bool success_without_id = raw.status_code >= 200 && raw.status_code < 300 &&
                                  !(cleanup && cleanup->type == MYBOT_JSON_FALSE);
        resp->start_unknown =
            !resp->conversation_id[0] && (resp->cleanup_required || success_without_id);
        if (!resp->error_code[0]) {
            set_start_error(resp, "CONVERSATION_START_REJECTED");
        }
        result = raw.status_code >= 400 ? raw.status_code : -1;
        goto done;
    }
    if (!resp->conversation_id[0]) {
        resp->start_unknown = true;
        set_start_error(resp, "START_RESULT_UNKNOWN");
        goto done;
    }
    if (parse_translation_route(data, mode, source_language, target_language, resp) < 0) {
        if (!resp->error_code[0]) {
            set_start_error(resp, "INVALID_TRANSLATION_ROUTE");
        }
        goto done;
    }
    resp->cleanup_required = false;
    result = 0;

done:
    if (result != 0) {
        AOSL_LOG_ERR("translation start failed: transport=%d http=%d code=%s cleanup_required=%d "
                     "start_unknown=%d response_bytes=%zu",
                     ret, raw.status_code, resp->error_code, resp->cleanup_required,
                     resp->start_unknown, raw.body_len);
    }
    mybot_json_delete(root);
    mybot_http_client_response_free(&raw);
    return result;
}

int mybot_device_client_stop_conversation(const char *base_url, const char *device_id,
                                          const char *device_token, const char *conversation_id,
                                          const char *reason) {
    if (!base_url || !device_id || !device_token || !conversation_id) {
        return -1;
    }

    char url[MYBOT_DEVICE_CLIENT_MAX_URL];
    if (build_device_url(base_url, device_id, "/conversations/stop", url, sizeof(url)) < 0) {
        return -1;
    }

    char *body = build_stop_conversation_body(conversation_id, reason);
    if (!body) {
        return -1;
    }

    AOSL_LOG_NTC("POST %s request body length=%zu", url, strlen(body));

    mybot_http_client_response_t raw;
    memset(&raw, 0, sizeof(raw));

    int ret = post_device_json(url, device_token, body, 0, &raw, NULL);
    mybot_json_free_string(body);

    if (ret == 0) {
        if (raw.status_code != 200) {
            ret = raw.status_code >= 400 ? raw.status_code : -1;
        } else {
            mybot_json_t *root = NULL;
            mybot_json_t *data = parse_response_data(&raw, &root);
            mybot_json_t *conversation = mybot_json_get_object_item(data, "conversation");
            ret = json_string_is(conversation, "status", "ended") ? 0 : -1;
            mybot_json_delete(root);
        }
    }

    mybot_http_client_response_free(&raw);
    return ret;
}
