/* SPDX-License-Identifier: MIT */
#include "host_input.h"
#include "input/ui_input.h"
#include "mybot_platform/board.h"
#include "mybot_platform/factory_reset.h"
#include "input/mosaico_keys.h"
#include <mybot/mybot.h>
#include <mybot/platform/mybot_key.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static std::atomic<unsigned> factory_resets;
static unsigned task_news;
static std::string failure;
static std::mutex event_lock;
static std::condition_variable event_changed;
static std::vector<mybot_key_event_t> events;
static std::vector<mybot_ui_command_t> commands;
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
esp_err_t gpio_config(const gpio_config_t *config) {
    CHECK(config->pin_bit_mask == (UINT64_C(1) << 7) && config->mode == GPIO_MODE_INPUT &&
          config->pull_up_en == GPIO_PULLUP_ENABLE &&
          config->pull_down_en == GPIO_PULLDOWN_DISABLE);
    return fail("gpio") ? ESP_FAIL : ESP_OK;
}
int gpio_get_level(int pin) {
    CHECK(pin == 7);
    std::unique_lock<std::mutex> guard(queue_lock);
    queue_changed.wait(guard, [] { return stop_worker || !samples.empty(); });
    if (stop_worker) {
        guard.unlock();
        pthread_exit(nullptr);
    }
    current = samples.front();
    samples.pop_front();
    time_us.fetch_add(20000);
    return current.mv == 1340 || current.mv == 1870 ? 0 : 1;
}
int xTaskCreateWithCaps(task_function_t function, const char *name, uint32_t bytes, void *arg,
                        unsigned priority, TaskHandle_t *out, unsigned caps) {
    CHECK(!task.open && std::string(name) == "mosaico_input" && bytes == 3072 && !arg);
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
int mybot_ui_submit(const mybot_ui_command_t *command) {
    CHECK(command);
    commands.push_back(*command);
    return 0;
}
void mybot_board_request_wifi_provisioning() {
    ++provisioning;
}
void mybot_factory_reset_request() {
    ++factory_resets;
}
int mybot_mosaico_keys_start();
const mybot_key_ops_t *mybot_mosaico_key_ops();
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
    mybot_ui_input_touch_sample(valid, pressed);
}

static void tap() {
    touch_sample(true, true);
    touch_sample(true, false);
    mybot_ui_input_event(state == MYBOT_STATE_IN_CONVERSATION ? MYBOT_KEY_EVENT_CONVERSATION_STOP
                                                              : MYBOT_KEY_EVENT_CONVERSATION_START);
}

static void button(mybot_key_event_t event) {
    touch_sample(true, true);
    touch_sample(true, false);
    mybot_ui_input_event(event);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const std::string scenario = argv[1];
    const mybot_key_ops_t *ops = mybot_mosaico_key_ops();
    void *ctx = reinterpret_cast<void *>(1);
    CHECK(ops->init(&ctx, emit, &events) < 0 && !ctx); // source not prepared
    if (scenario == "mutex" || scenario == "gpio" || scenario == "task") {
        failure = scenario;
        CHECK(mybot_mosaico_keys_start() < 0 && !task.open);
    }
    CHECK(mybot_mosaico_keys_start() == 0 && task.open);
    CHECK(mybot_mosaico_keys_start() == 0 && task_news == 1);
    CHECK(ops->init(&ctx, emit, &events) == 0 && ctx);
    void *other = reinterpret_cast<void *>(1);
    CHECK(ops->init(&other, emit, &events) < 0 && !other);
    if (scenario == "gpio_behavior") {
        feed(1340, 170);
        CHECK(events.empty() && provisioning == 0 && factory_resets == 0);
    }
    feed(2000, 4);
    if (scenario == "gpio_behavior") {
        feed(1340, 2);
        feed(2000, 4);
        CHECK(events.empty()); // bounce
        press(1340);
        CHECK(events.empty() && provisioning == 0 && factory_resets == 0);
        state = MYBOT_STATE_IN_CONVERSATION;
        press(1340);
        CHECK(events.empty() && provisioning == 0 && factory_resets == 0);
        feed(1340, 140);
        feed(2000, 4);
        CHECK(events.empty() && provisioning == 0 && factory_resets == 0);
        feed(1340, 160);
        CHECK(provisioning == 1);
        feed(1340, 20);
        feed(2000, 4);
        CHECK(events.empty() && provisioning == 1 && factory_resets == 0);
        feed(1340, 8);
        feed(1340, 490);
        CHECK(provisioning == 2 && factory_resets == 0); // reset requires ten full seconds
        feed(1340, 20);
        CHECK(provisioning == 2 && factory_resets == 1);
        feed(1340, 20);
        CHECK(factory_resets == 1);
        feed(2000, 4);
        CHECK(events.empty());
        ops->destroy(ctx);
        feed(1340, 160);
        feed(2000, 4);
        CHECK(provisioning == 3 && factory_resets == 1 && events.empty());
        CHECK(ops->init(&ctx, emit, &events) == 0);
    } else if (scenario == "touch") {
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
        mybot_ui_input_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
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
    } else if (scenario == "late_cancel") {
        touch_sample(true, false);
        state = MYBOT_STATE_TRANSLATION_STARTING;
        touch_sample(true, true);
        state = MYBOT_STATE_IN_CONVERSATION;
        touch_sample(true, false);
        mybot_ui_input_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        feed(2000, 1);
        CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_TRANSLATION_STARTING;
        button(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_IN_CONVERSATION;
        feed(2000, 1);
        CHECK(events.size() == 2 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_TRANSLATION_STARTING;
        button(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_READY;
        feed(2000, 1);
        CHECK(events.size() == 2); // completed teardown must not affect another start
        state = MYBOT_STATE_TRANSLATION_STARTING;
        touch_sample(true, true);
        state = MYBOT_STATE_IN_CONVERSATION;
        touch_sample(true, false);
        mybot_ui_input_event(MYBOT_KEY_EVENT_VOLUME_UP);
        feed(2000, 1);
        CHECK(events.size() == 2); // only stop can cross the start-completion boundary
    } else if (scenario == "ui_commands") {
        touch_sample(true, false);
        mybot_ui_command_t command{};
        command.type = MYBOT_UI_SET_VOLUME;
        command.value = 40;
        auto submit = [&] {
            touch_sample(true, true);
            touch_sample(true, false);
            mybot_ui_input_command(&command);
        };
        submit();
        CHECK(commands.empty()); // GUI copies only; the worker invokes the SDK.
        command.value = 99;
        feed(2000, 1);
        CHECK(commands.size() == 1 && commands.back().value == 40);
        command.value = 65;
        for (unsigned i = 0; i < 20; ++i)
            submit();
        feed(2000, 1);
        CHECK(commands.size() == 2 && commands.back().value == 65); // bounded mailbox
        command.type = MYBOT_UI_SET_LANGUAGES;
        std::snprintf(command.source_language, sizeof(command.source_language), "zh-CN");
        std::snprintf(command.target_language, sizeof(command.target_language), "en-US");
        submit();
        feed(2000, 1);
        CHECK(commands.size() == 3 && !std::strcmp(commands.back().source_language, "zh-CN"));
        state = MYBOT_STATE_IN_CONVERSATION;
        submit();
        feed(2000, 1);
        CHECK(commands.size() == 3); // language changes freeze during streaming
        command.type = MYBOT_UI_SET_PAUSED;
        command.value = 1;
        submit();
        feed(2000, 1);
        CHECK(commands.size() == 4 && commands.back().type == MYBOT_UI_SET_PAUSED);
        submit();
        state = MYBOT_STATE_READY;
        feed(2000, 1);
        CHECK(commands.size() == 4); // state changed before worker delivery
        submit();
        feed(2000, 1);
        CHECK(commands.size() == 4); // pause requires an active session
        command.type = MYBOT_UI_SET_VOLUME;
        command.value = 101;
        submit();
        feed(2000, 1);
        CHECK(commands.size() == 4);
        command.value = 20;
        submit();
        ops->destroy(ctx);
        CHECK(ops->init(&ctx, emit, &events) == 0);
        feed(2000, 1);
        CHECK(commands.size() == 4); // old attachment generation cannot replay
        touch_sample(true, true);
        touch_sample(false, false);
        touch_sample(true, false);
        mybot_ui_input_command(&command);
        feed(2000, 1);
        CHECK(commands.size() == 4); // read error cancels the gesture
        submit();
        feed(2000, 1);
        CHECK(commands.size() == 5 && commands.back().value == 20);
    } else if (scenario == "ui_controls") {
        touch_sample(true, false);
        touch_sample(true, true);
        touch_sample(true, false);
        feed(2000, 1);
        CHECK(events.empty()); // raw touches never start translation
        button(MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT);
        feed(2000, 1);
        CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT);
        button(MYBOT_KEY_EVENT_TARGET_LANGUAGE_NEXT);
        feed(2000, 1);
        CHECK(events.size() == 2 && events.back() == MYBOT_KEY_EVENT_TARGET_LANGUAGE_NEXT);
        state = MYBOT_STATE_IN_CONVERSATION;
        button(MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT);
        feed(2000, 1);
        CHECK(events.size() == 2); // languages freeze while streaming
        button(MYBOT_KEY_EVENT_VOLUME_DOWN);
        feed(2000, 1);
        CHECK(events.size() == 3 && events.back() == MYBOT_KEY_EVENT_VOLUME_DOWN);
        state = MYBOT_STATE_TRANSLATION_STARTING;
        button(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        feed(2000, 1);
        CHECK(events.size() == 4 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_CLEANUP_PENDING;
        button(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        feed(2000, 1);
        CHECK(events.size() == 5 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        state = MYBOT_STATE_START_UNKNOWN;
        button(MYBOT_KEY_EVENT_CONVERSATION_START);
        feed(2000, 1);
        CHECK(events.size() == 5);
        state = MYBOT_STATE_TRANSLATION_ERROR;
        button(MYBOT_KEY_EVENT_CONVERSATION_START);
        feed(2000, 1);
        CHECK(events.size() == 6 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
        state = MYBOT_STATE_READY;
        touch_sample(true, true);
        touch_sample(false, false);
        touch_sample(true, false);
        mybot_ui_input_event(MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT);
        feed(2000, 1);
        CHECK(events.size() == 6); // I2C failure cannot synthesize a button click
    } else if (scenario == "touch_ordering") {
        block_callback = true;
        touch_sample(true, false);
        tap();
        std::thread feeder([&] { feed(2000, 1); });
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
        button(MYBOT_KEY_EVENT_VOLUME_DOWN);
        feed(2000, 1);
        CHECK(events.size() == 1); // no use of detached user data
        CHECK(ops->init(&ctx, emit, &events) == 0);
        button(MYBOT_KEY_EVENT_VOLUME_DOWN);
        feed(2000, 1);
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
    CHECK(task_news == 1); // board process-lifetime source is preserved
    std::printf("PASS mosaico_keys %s\n", argv[1]);
}
