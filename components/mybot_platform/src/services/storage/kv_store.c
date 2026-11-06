/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/platform/mybot_kv_store.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG "mybot_kv"
#define MYBOT_NVS_NAMESPACE "mybot"

typedef struct {
    nvs_handle_t handle;
    bool open;
} kv_context_t;

static kv_context_t s_context;

/* Report storage pressure without exposing credentials or serialized records. */
static void log_storage_state(const char *trigger) {
    nvs_stats_t stats = {0};
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "event=nvs_stats trigger=%s partition=nvs used=%zu free=%zu available=%zu "
                 "total=%zu namespaces=%zu",
                 trigger, stats.used_entries, stats.free_entries, stats.available_entries,
                 stats.total_entries, stats.namespace_count);
    } else {
        ESP_LOGW(TAG, "event=nvs_stats trigger=%s result=error error=%s code=0x%x", trigger,
                 esp_err_to_name(err), (unsigned)err);
    }
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,
             "event=storage_heap trigger=%s internal_free=%zu internal_largest=%zu "
             "psram_free=%zu psram_largest=%zu",
             trigger, heap_caps_get_free_size(internal), heap_caps_get_largest_free_block(internal),
             heap_caps_get_free_size(psram), heap_caps_get_largest_free_block(psram));
}

static void log_failure(const char *operation, const char *key, size_t bytes, esp_err_t err) {
    ESP_LOGE(TAG, "event=kv operation=%s key=%s bytes=%zu result=error error=%s code=0x%x",
             operation, key ? key : "-", bytes, esp_err_to_name(err), (unsigned)err);
    log_storage_state("error");
}

static int kv_init(void **out_ctx) {
    if (!out_ctx || s_context.open) {
        return -1;
    }
    *out_ctx = NULL;
    esp_err_t err = nvs_open(MYBOT_NVS_NAMESPACE, NVS_READWRITE, &s_context.handle);
    if (err != ESP_OK) {
        log_failure("open", NULL, 0, err);
        return -1;
    }
    s_context.open = true;
    *out_ctx = &s_context;
    log_storage_state("open");
    return 0;
}

static int kv_get(void *opaque, const char *key, void *value, size_t capacity, size_t *out_len) {
    kv_context_t *ctx = opaque;
    if (ctx != &s_context || !ctx->open || !key || !value || !out_len) {
        return -1;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_blob(ctx->handle, key, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return MYBOT_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        log_failure("get_size", key, capacity, err);
        return -1;
    }
    if (required > capacity) {
        ESP_LOGE(TAG, "event=kv operation=get_capacity key=%s required=%zu capacity=%zu", key,
                 required, capacity);
        log_failure("get_capacity", key, required, ESP_ERR_NVS_INVALID_LENGTH);
        return -1;
    }

    size_t actual = capacity;
    err = nvs_get_blob(ctx->handle, key, value, &actual);
    if (err != ESP_OK) {
        log_failure("get_data", key, capacity, err);
        return -1;
    }
    *out_len = actual;
    return 0;
}

static int kv_set(void *opaque, const char *key, const void *value, size_t len) {
    kv_context_t *ctx = opaque;
    if (ctx != &s_context || !ctx->open || !key || (!value && len != 0)) {
        return -1;
    }
    esp_err_t err = nvs_set_blob(ctx->handle, key, value, len);
    if (err != ESP_OK) {
        log_failure("set_blob", key, len, err);
        return -1;
    }
    err = nvs_commit(ctx->handle);
    if (err != ESP_OK) {
        log_failure("set_commit", key, len, err);
        return -1;
    }
    ESP_LOGI(TAG, "event=kv operation=set key=%s bytes=%zu result=ok", key, len);
    return 0;
}

static int kv_erase(void *opaque, const char *key) {
    kv_context_t *ctx = opaque;
    if (ctx != &s_context || !ctx->open || !key) {
        return -1;
    }
    esp_err_t err = nvs_erase_key(ctx->handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        log_failure("erase_key", key, 0, err);
        return -1;
    }
    err = nvs_commit(ctx->handle);
    if (err != ESP_OK) {
        log_failure("erase_commit", key, 0, err);
        return -1;
    }
    return 0;
}

static void kv_destroy(void *opaque) {
    kv_context_t *ctx = opaque;
    if (ctx != &s_context || !ctx->open) {
        return;
    }
    nvs_close(ctx->handle);
    ctx->open = false;
}

static const mybot_kv_store_ops_t s_ops = {
    .init = kv_init,
    .get = kv_get,
    .set = kv_set,
    .erase = kv_erase,
    .destroy = kv_destroy,
};

const mybot_kv_store_ops_t *mybot_esp_idf_kv_store_ops(void) {
    return &s_ops;
}
