/* SPDX-License-Identifier: MIT */
#include "host_input.h"
#include "mybot_platform/board.h"
#include "input/korvo_s31_keys.h"
#include <mybot/mybot.h>
#include <mybot/platform/mybot_key.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #test);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

struct host_adc {
    bool open = false;
} adc;
struct host_task {
    bool open = false;
} task;
struct Sample {
    int mv;
    bool read_ok, calibration_ok;
} current;
static std::thread worker;
static std::mutex queue_lock;
static std::condition_variable queue_changed;
static std::deque<Sample> samples;
static unsigned completed;
static bool stop_worker;
static std::atomic<int64_t> time_us;
static std::atomic<mybot_state_t> state{MYBOT_STATE_READY};
static std::atomic<unsigned> provisioning;
static unsigned adc_news, task_news;
static std::string failure;
static unsigned cleanup_failures;
static bool partial_new;
static std::mutex event_lock;
static std::condition_variable event_changed;
static std::vector<mybot_key_event_t> events;
static bool block_callback, entered_callback, release_callback;

static bool fail(const char *name) {
    if (failure != name)
        return false;
    failure.clear();
    return true;
}

extern "C" {
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) {
    if (fail("mutex"))
        return nullptr;
    CHECK(pthread_mutex_init(storage, nullptr) == 0);
    return storage;
}
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    CHECK(semaphore && ticks == portMAX_DELAY);
    CHECK(pthread_mutex_lock(semaphore) == 0);
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    CHECK(pthread_mutex_unlock(semaphore) == 0);
    return pdTRUE;
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *config,
                               adc_oneshot_unit_handle_t *out) {
    CHECK(config->unit_id == ADC_UNIT_1 && !adc.open);
    ++adc_news;
    if (fail("adc_new")) {
        if (partial_new) {
            adc.open = true;
            *out = &adc;
        }
        return ESP_FAIL;
    }
    adc.open = true;
    *out = &adc;
    return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config) {
    CHECK(handle == &adc && adc.open && channel == ADC_CHANNEL_0);
    CHECK(config->atten == ADC_ATTEN_DB_0 && config->bitwidth == ADC_BITWIDTH_DEFAULT);
    return fail("channel") ? ESP_FAIL : ESP_OK;
}
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle) {
    CHECK(handle == &adc && adc.open && !task.open);
    if (cleanup_failures) {
        --cleanup_failures;
        return ESP_FAIL;
    }
    adc.open = false;
    return ESP_OK;
}
esp_err_t bsp_s31_adc_calibration_init(adc_oneshot_unit_handle_t handle, adc_unit_t unit,
                                       adc_channel_t channel) {
    CHECK(handle == &adc && unit == ADC_UNIT_1 && channel == ADC_CHANNEL_0 && adc.open);
    return fail("calibrate") ? ESP_FAIL : ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t channel, int *raw) {
    CHECK(handle == &adc && adc.open && channel == ADC_CHANNEL_0 && raw);
    std::unique_lock<std::mutex> guard(queue_lock);
    queue_changed.wait(guard, [] { return stop_worker || !samples.empty(); });
    if (stop_worker) {
        guard.unlock();
        pthread_exit(nullptr);
    }
    current = samples.front();
    samples.pop_front();
    time_us.fetch_add(20000);
    *raw = current.mv;
    return current.read_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t bsp_s31_adc_calibration_raw_to_mv(adc_unit_t unit, int raw, int *mv) {
    CHECK(unit == ADC_UNIT_1);
    *mv = raw;
    return current.calibration_ok ? ESP_OK : ESP_FAIL;
}
int xTaskCreateWithCaps(task_function_t function, const char *name, uint32_t bytes, void *arg,
                        unsigned priority, TaskHandle_t *out, unsigned caps) {
    CHECK(adc.open && !task.open && std::string(name) == "korvo_keys" && bytes == 3072 && !arg);
    CHECK(priority == 4 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail("task"))
        return pdFALSE;
    ++task_news;
    task.open = true;
    *out = &task;
    worker = std::thread([=] { function(arg); });
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) {
    CHECK(ticks == 20);
    std::lock_guard<std::mutex> guard(queue_lock);
    ++completed;
    queue_changed.notify_all();
}
int64_t esp_timer_get_time() {
    return time_us.load();
}
mybot_state_t mybot_get_state() {
    return state.load();
}
void mybot_board_request_wifi_provisioning() {
    ++provisioning;
}
int mybot_korvo_s31_keys_start();
const mybot_key_ops_t *mybot_korvo_s31_key_ops();
}

static void feed(int mv, unsigned count, bool read_ok = true, bool calibration_ok = true) {
    for (unsigned i = 0; i < count; ++i) {
        std::unique_lock<std::mutex> guard(queue_lock);
        const unsigned target = completed + 1;
        samples.push_back({mv, read_ok, calibration_ok});
        queue_changed.notify_all();
        CHECK(queue_changed.wait_for(guard, std::chrono::seconds(3),
                                     [&] { return completed == target; }));
    }
}

static void emit(mybot_key_event_t event, void *opaque) {
    CHECK(opaque == &events);
    std::unique_lock<std::mutex> guard(event_lock);
    entered_callback = true;
    event_changed.notify_all();
    if (block_callback)
        event_changed.wait(guard, [] { return release_callback; });
    events.push_back(event);
}

static void press(int mv) {
    feed(mv, 8);
    feed(2000, 4);
}

static void touch_sample(bool valid, bool pressed) {
    mybot_korvo_s31_keys_touch_sample(valid, pressed);
}

static void tap() {
    touch_sample(true, true);
    touch_sample(true, false);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const std::string scenario = argv[1];
    const mybot_key_ops_t *ops = mybot_korvo_s31_key_ops();
    void *ctx = reinterpret_cast<void *>(1);
    CHECK(ops->init(&ctx, emit, &events) < 0 && !ctx); // source not prepared
    if (scenario != "behavior" && scenario != "ordering" && scenario != "touch" &&
        scenario != "touch_stale" && scenario != "touch_ordering") {
        failure = scenario == "cleanup_retry" ? "channel" : scenario;
        if (scenario == "partial_adc_new") {
            failure = "adc_new";
            partial_new = true;
        }
        if (scenario == "cleanup_retry")
            cleanup_failures = 2;
        CHECK(mybot_korvo_s31_keys_start() < 0 && !task.open);
        if (scenario == "cleanup_retry") {
            CHECK(adc.open && adc_news == 1);
            CHECK(mybot_korvo_s31_keys_start() < 0 && adc.open && adc_news == 1);
        } else
            CHECK(!adc.open);
    }
    CHECK(mybot_korvo_s31_keys_start() == 0 && task.open && adc.open);
    const unsigned news = adc_news;
    CHECK(mybot_korvo_s31_keys_start() == 0 && adc_news == news && task_news == 1);
    CHECK(ops->init(&ctx, emit, &events) == 0 && ctx);
    void *other = reinterpret_cast<void *>(1);
    CHECK(ops->init(&other, emit, &events) < 0 && !other);
    feed(2000, 4);
    if (scenario == "touch") {
        tap();
        feed(2000, 1);
        CHECK(events.empty()); // screen started with a finger down: require a clean release
        tap();
        CHECK(events.empty()); // UI callback only posts; SDK work belongs to key worker
        feed(2000, 1);
        CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
        state = MYBOT_STATE_IN_CONVERSATION;
        tap();
        feed(2000, 1);
        CHECK(events.size() == 2 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        for (unsigned i = 0; i < 200; ++i) {
            touch_sample(true, true);
        }
        feed(2000, 2);
        CHECK(events.size() == 2); // holding never repeats
        touch_sample(true, false);
        feed(2000, 1);
        CHECK(events.size() == 3 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        touch_sample(true, false);
        feed(2000, 1);
        CHECK(events.size() == 3); // release cannot retrigger
        touch_sample(true, true);
        touch_sample(false, false);
        touch_sample(true, true);
        touch_sample(true, false);
        feed(2000, 1);
        CHECK(events.size() == 3); // read error cancels until a clean release
        tap();
        feed(2000, 1);
        CHECK(events.size() == 4);
        for (unsigned i = 0; i < 20; ++i)
            tap();
        feed(2000, 1);
        CHECK(events.size() == 5); // bounded mailbox does not queue a burst
        for (unsigned i = 0; i < 20; ++i) {
            state = (i & 1U) ? MYBOT_STATE_IN_CONVERSATION : MYBOT_STATE_READY;
            tap();
            feed(2000, 1);
            CHECK(events.size() == 6 + i);
            CHECK(events.back() == ((i & 1U) ? MYBOT_KEY_EVENT_CONVERSATION_STOP
                                             : MYBOT_KEY_EVENT_CONVERSATION_START));
        }
    } else if (scenario == "touch_stale") {
        touch_sample(true, false);
        state = MYBOT_STATE_PAIRING;
        touch_sample(true, true);
        state = MYBOT_STATE_READY;
        touch_sample(true, false);
        feed(2000, 1);
        CHECK(events.empty()); // a press begun before readiness cannot become a command
        touch_sample(true, true);
        state = MYBOT_STATE_PAIRING;
        touch_sample(true, false);
        state = MYBOT_STATE_READY;
        feed(2000, 1);
        CHECK(events.empty()); // release while not ready is discarded at source
        tap();
        state = MYBOT_STATE_PAIRING;
        feed(2000, 1);
        state = MYBOT_STATE_READY;
        feed(2000, 1);
        CHECK(events.empty()); // worker discards a command invalidated after release
        ops->destroy(ctx);
        tap();
        CHECK(ops->init(&ctx, emit, &events) == 0);
        feed(2000, 1);
        CHECK(events.empty()); // detached touches are never delayed to a new SDK
        touch_sample(true, true);
        ops->destroy(ctx);
        CHECK(ops->init(&ctx, emit, &events) == 0);
        touch_sample(true, false);
        feed(2000, 1);
        CHECK(events.empty()); // held finger cannot cross SDK attachment generations
        tap();
        ops->destroy(ctx);
        CHECK(ops->init(&ctx, emit, &events) == 0);
        feed(2000, 1);
        CHECK(events.empty()); // queued command is cleared by detach
        tap();
        feed(2000, 1);
        CHECK(events.size() == 1);
    } else if (scenario == "behavior") {
        feed(1340, 2);
        feed(2000, 4); // bounce: never stable
        CHECK(events.empty());
        press(1340);
        CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
        state = MYBOT_STATE_IN_CONVERSATION;
        press(1340);
        CHECK(events.size() == 2 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_PAIRING;
        press(1340);
        CHECK(events.size() == 2);
        press(380);
        CHECK(events.back() == MYBOT_KEY_EVENT_VOLUME_UP);
        press(820);
        CHECK(events.back() == MYBOT_KEY_EVENT_VOLUME_DOWN);
        press(1870);
        CHECK(events.back() == MYBOT_KEY_EVENT_PAIR);
        const size_t before = events.size();
        feed(1340, 6);
        feed(0, 1, false);
        feed(2000, 4);
        CHECK(events.size() == before); // ADC error cancels a stable press
        feed(1340, 6);
        feed(0, 1, true, false);
        feed(2000, 4);
        CHECK(events.size() == before); // calibration error also cancels
        ops->destroy(ctx);
        state = MYBOT_STATE_STOPPED;
        press(380);
        CHECK(events.size() == before);
        feed(1870, 160);
        CHECK(provisioning == 1 && events.size() == before);
        feed(1870, 20);
        feed(2000, 4);
        CHECK(provisioning == 1 && events.size() == before); // no PAIR after long SET
        CHECK(ops->init(&ctx, emit, &events) == 0);
        press(1870);
        CHECK(events.size() == before + 1 && events.back() == MYBOT_KEY_EVENT_PAIR);
        feed(1870, 40);
        feed(0, 1, false);
        feed(2000, 160);
        CHECK(provisioning == 1); // no stale long press after invalid sample
    } else if (scenario == "ordering" || scenario == "touch_ordering") {
        block_callback = true;
        if (scenario == "touch_ordering") {
            touch_sample(true, false);
            tap();
        }
        std::thread feeder([&] {
            if (scenario == "touch_ordering")
                feed(2000, 1);
            else
                press(380);
        });
        {
            std::unique_lock<std::mutex> guard(event_lock);
            CHECK(event_changed.wait_for(guard, std::chrono::seconds(3),
                                         [] { return entered_callback; }));
        }
        std::atomic<bool> detached{false};
        std::thread detach([&] {
            ops->destroy(ctx);
            detached = true;
        });
        {
            std::unique_lock<std::mutex> guard(event_lock);
            CHECK(!event_changed.wait_for(guard, std::chrono::milliseconds(40),
                                          [&] { return detached.load(); }));
            CHECK(events.empty());
            // LVGL can still report a touch while emit and detach wait. No callback
            // mutex is taken here, so display teardown cannot deadlock with SDK stop.
            tap();
            release_callback = true;
            event_changed.notify_all();
        }
        feeder.join();
        detach.join();
        CHECK(detached && events.size() == 1);
        feed(2000, 1);
        CHECK(events.size() == 1); // pending touch was canceled by detach
        press(820);
        CHECK(events.size() == 1); // no use of detached user data
        CHECK(ops->init(&ctx, emit, &events) == 0);
        press(820);
        CHECK(events.size() == 2 && events.back() == MYBOT_KEY_EVENT_VOLUME_DOWN);
    }
    ops->destroy(ctx);
    ops->destroy(ctx);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(ops->init(&ctx, emit, &events) == 0);
        ops->destroy(ctx);
    }
    {
        std::lock_guard<std::mutex> guard(queue_lock);
        stop_worker = true;
        queue_changed.notify_all();
    }
    worker.join();
    CHECK(adc.open && task_news == 1); // board process-lifetime source is preserved
    std::printf("PASS korvo_s31_keys %s\n", argv[1]);
}
