/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/platform/mybot_kv_store.h>
#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const mybot_kv_store_ops_t *mybot_esp_idf_kv_store_ops(void);

static esp_err_t open_error, size_error, read_error, set_error, commit_error, erase_error,
    stats_error;
static unsigned opens, reads, sets, commits, erases, closes, stats_calls;
static size_t required;
static char logs[16384];
static unsigned char record[1456];
static const char secret[] = "credential_value_must_never_appear_in_diagnostics";

void test_log(const char *tag, const char *format, ...) {
    assert(!strcmp(tag, "mybot_kv"));
    size_t offset = strlen(logs);
    va_list args;
    va_start(args, format);
    int size = vsnprintf(logs + offset, sizeof(logs) - offset, format, args);
    va_end(args);
    assert(size >= 0 && (size_t)size + 1 < sizeof(logs) - offset);
    strcat(logs, "\n");
    assert(!strstr(logs, secret));
}
const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NVS_NOT_FOUND:
        return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_TYPE_MISMATCH:
        return "ESP_ERR_NVS_TYPE_MISMATCH";
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
    case ESP_ERR_NVS_INVALID_LENGTH:
        return "ESP_ERR_NVS_INVALID_LENGTH";
    case ESP_ERR_NVS_NO_FREE_PAGES:
        return "ESP_ERR_NVS_NO_FREE_PAGES";
    default:
        assert(0);
        return "unknown";
    }
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "mybot") && mode == NVS_READWRITE && handle);
    ++opens;
    if (!open_error)
        *handle = 3;
    return open_error;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length) {
    assert(handle == 3 && !strcmp(key, "tr_session") && length);
    ++reads;
    if (!value) {
        *length = required;
        return size_error;
    }
    if (!read_error) {
        assert(*length >= required);
        memcpy(value, record, required);
        *length = required;
    }
    return read_error;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    assert(handle == 3 && !strcmp(key, "tr_session"));
    assert((length == sizeof(record) && value && !memcmp(value, record, length)) ||
           (length == 0 && !value));
    ++sets;
    return set_error;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 3);
    ++commits;
    return commit_error;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    assert(handle == 3 && !strcmp(key, "tr_session"));
    ++erases;
    return erase_error;
}
esp_err_t nvs_get_stats(const char *partition, nvs_stats_t *stats) {
    assert(!partition && stats);
    ++stats_calls;
    if (!stats_error)
        *stats = (nvs_stats_t){.used_entries = 350,
                               .free_entries = 154,
                               .available_entries = 28,
                               .total_entries = 504,
                               .namespace_count = 8};
    return stats_error;
}
void nvs_close(nvs_handle_t handle) {
    assert(handle == 3);
    ++closes;
}
size_t heap_caps_get_free_size(uint32_t caps) {
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) ||
           caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return caps & MALLOC_CAP_INTERNAL ? 65536 : 8388608;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) {
    return heap_caps_get_free_size(caps) / 2;
}
static void reset_calls(void) {
    opens = reads = sets = commits = erases = closes = stats_calls = 0;
    logs[0] = '\0';
    open_error = size_error = read_error = set_error = commit_error = erase_error = stats_error =
        ESP_OK;
    required = sizeof(record);
}
static void check_error(const char *operation, const char *name, const char *code) {
    assert(strstr(logs, operation) && strstr(logs, name) && strstr(logs, code));
    assert(stats_calls == 1);
    assert(strstr(logs, "used=350 free=154 available=28 total=504 namespaces=8"));
    assert(strstr(logs, "internal_free=65536 internal_largest=32768"));
    assert(strstr(logs, "psram_free=8388608 psram_largest=4194304"));
}
int main(void) {
    memcpy(record, secret, sizeof(secret));
    const mybot_kv_store_ops_t *ops = mybot_esp_idf_kv_store_ops();
    void *context = (void *)1;
    assert(ops->init(NULL) == -1 && opens == 0);
    open_error = ESP_ERR_NVS_NO_FREE_PAGES;
    assert(ops->init(&context) == -1 && context == NULL && opens == 1);
    check_error("operation=open", "ESP_ERR_NVS_NO_FREE_PAGES", "code=0x110d");
    reset_calls();
    stats_error = ESP_ERR_INVALID_STATE;
    assert(ops->init(&context) == 0 && context && opens == 1 && stats_calls == 1);
    assert(strstr(logs, "event=nvs_stats trigger=open result=error error=ESP_ERR_INVALID_STATE"));
    assert(ops->init(NULL) == -1 && opens == 1);
    ops->destroy(context);
    assert(closes == 1);
    reset_calls();
    assert(ops->init(&context) == 0 && context && stats_calls == 1);
    assert(strstr(logs, "event=nvs_stats trigger=open"));
    assert(ops->init(&context) == -1 && opens == 1 && stats_calls == 1);

    unsigned char output[sizeof(record)];
    size_t length = 777;
    reset_calls();
    size_error = ESP_ERR_NVS_NOT_FOUND;
    assert(ops->get(context, "tr_session", output, sizeof(output), &length) == MYBOT_ERR_NOT_FOUND);
    assert(length == 777 && reads == 1 && stats_calls == 0 && !logs[0]);
    reset_calls();
    size_error = ESP_ERR_NVS_TYPE_MISMATCH;
    assert(ops->get(context, "tr_session", output, sizeof(output), &length) == -1);
    assert(length == 777 && reads == 1);
    check_error("operation=get_size", "ESP_ERR_NVS_TYPE_MISMATCH", "code=0x1103");
    reset_calls();
    assert(ops->get(context, "tr_session", output, 32, &length) == -1);
    assert(length == 777 && reads == 1 && strstr(logs, "required=1456 capacity=32"));
    check_error("operation=get_capacity", "ESP_ERR_NVS_INVALID_LENGTH", "code=0x110c");
    reset_calls();
    read_error = ESP_ERR_NVS_INVALID_LENGTH;
    assert(ops->get(context, "tr_session", output, sizeof(output), &length) == -1);
    assert(length == 777 && reads == 2);
    check_error("operation=get_data", "ESP_ERR_NVS_INVALID_LENGTH", "code=0x110c");
    reset_calls();
    assert(ops->get(context, "tr_session", output, sizeof(output), &length) == 0);
    assert(length == sizeof(record) && !memcmp(output, record, length) && reads == 2 && !logs[0]);

    reset_calls();
    set_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    assert(ops->set(context, "tr_session", record, sizeof(record)) == -1);
    assert(sets == 1 && commits == 0 && erases == 0);
    check_error("operation=set_blob key=tr_session bytes=1456", "ESP_ERR_NVS_NOT_ENOUGH_SPACE",
                "code=0x1105");
    reset_calls();
    commit_error = ESP_FAIL;
    assert(ops->set(context, "tr_session", record, sizeof(record)) == -1);
    assert(sets == 1 && commits == 1 && erases == 0 && !strstr(logs, "result=ok"));
    check_error("operation=set_commit", "ESP_FAIL", "code=0xffffffff");
    reset_calls();
    assert(ops->set(context, "tr_session", record, sizeof(record)) == 0);
    assert(sets == 1 && commits == 1 && erases == 0 && stats_calls == 0);
    assert(strstr(logs, "operation=set key=tr_session bytes=1456 result=ok"));
    reset_calls();
    set_error = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    stats_error = ESP_ERR_INVALID_STATE;
    assert(ops->set(context, "tr_session", record, sizeof(record)) == -1);
    assert(sets == 1 && commits == 0 && erases == 0 && stats_calls == 1);
    assert(strstr(logs, "operation=set_blob") && strstr(logs, "ESP_ERR_NVS_NOT_ENOUGH_SPACE"));
    assert(strstr(logs, "event=nvs_stats trigger=error result=error error=ESP_ERR_INVALID_STATE"));
    reset_calls();
    assert(ops->set(context, "tr_session", NULL, 1) == -1 && sets == 0);
    assert(ops->set(context, "tr_session", NULL, 0) == 0 && sets == 1 && commits == 1);

    reset_calls();
    erase_error = ESP_ERR_NVS_NOT_FOUND;
    assert(ops->erase(context, "tr_session") == 0 && erases == 1 && commits == 1 && !logs[0]);
    reset_calls();
    erase_error = ESP_FAIL;
    assert(ops->erase(context, "tr_session") == -1 && erases == 1 && commits == 0);
    check_error("operation=erase_key", "ESP_FAIL", "code=0xffffffff");
    reset_calls();
    commit_error = ESP_FAIL;
    assert(ops->erase(context, "tr_session") == -1 && erases == 1 && commits == 1);
    check_error("operation=erase_commit", "ESP_FAIL", "code=0xffffffff");
    reset_calls();
    assert(ops->erase(context, "tr_session") == 0 && erases == 1 && commits == 1);
    ops->destroy(NULL);
    assert(closes == 0);
    ops->destroy(context);
    ops->destroy(context);
    assert(closes == 1);
    assert(ops->set(context, "tr_session", record, sizeof(record)) == -1 && sets == 0);
    puts(
        "PASS kv_store error propagation, commit ordering, diagnostics and record confidentiality");
}
