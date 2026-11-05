/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Build the production pump and initialization code with bounded fake FIFOs.
 * Callbacks execute synchronously at injection points; no USB hardware is implied. */
#include "../../main/usb_cdc_probe.c"

static unsigned tests, checks, critical_depth, callback_depth;
static unsigned task_lock_entries, isr_lock_entries, isr_critical_depth;
static unsigned logs, controller_dumps, interrupt_dumps, stack_queries;
static bool in_isr, stack_connected, stack_mounted;
static char last_log[1024];
static unsigned reads, writes, flushes, rx_clears, tx_clears, notifications;
static unsigned installs, cdc_inits, tasks, cdc_deinits, uninstalls;
static bool owner, connected, bad_read_length, bad_write_length;
static bool driver_live, cdc_live;
static size_t read_limit, write_limit;
static esp_err_t read_result, flush_result, install_result, cdc_result, deinit_result;
static esp_err_t uninstall_result;
static BaseType_t create_result;
static uint8_t rx_fifo[4096], tx_fifo[4096];
static size_t rx_length, tx_length;
static char operations[32];
static unsigned operation_count;
static int worker_token;
static void (*after_read)(void);

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        assert(condition);                                                                         \
        ++checks;                                                                                  \
    } while (0)

void test_log(const char *tag, const char *format, ...) {
    CHECK(tag && format && !in_isr && !isr_critical_depth && !critical_depth && !callback_depth);
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(last_log, sizeof(last_log), format, arguments);
    va_end(arguments);
    CHECK(length >= 0 && (size_t)length < sizeof(last_log));
    logs++;
}
void test_enter_critical(portMUX_TYPE *lock) {
    CHECK((lock == &s_link_lock || lock == &s_bus_lock) && !critical_depth && !in_isr &&
          !isr_critical_depth);
    task_lock_entries++;
    critical_depth++;
}
void test_exit_critical(portMUX_TYPE *lock) {
    CHECK((lock == &s_link_lock || lock == &s_bus_lock) && critical_depth == 1 && !in_isr);
    critical_depth--;
}
void test_enter_critical_isr(portMUX_TYPE *lock) {
    CHECK(lock == &s_bus_lock && in_isr && !critical_depth && !isr_critical_depth);
    isr_lock_entries++;
    isr_critical_depth++;
}
void test_exit_critical_isr(portMUX_TYPE *lock) {
    CHECK(lock == &s_bus_lock && in_isr && isr_critical_depth == 1 && !critical_depth);
    isr_critical_depth--;
}
void mybot_usb_cdc_probe_log_controller(void) {
    CHECK(!in_isr && !callback_depth && !critical_depth && !isr_critical_depth && driver_live);
    controller_dumps++;
}
void mybot_usb_cdc_probe_log_interrupts(void) {
    CHECK(!in_isr && !callback_depth && !critical_depth && !isr_critical_depth && driver_live);
    interrupt_dumps++;
}
static void require_owner(void) {
    CHECK(owner && !callback_depth && !critical_depth && !in_isr && !isr_critical_depth);
}
static void record(char operation) {
    CHECK(operation_count + 1 < sizeof(operations));
    operations[operation_count++] = operation;
    operations[operation_count] = 0;
}
void xTaskNotifyGive(TaskHandle_t task) {
    CHECK(task == &worker_token && !critical_depth && !isr_critical_depth && !in_isr);
    notifications++;
}
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack, void *argument,
                       unsigned priority, TaskHandle_t *handle) {
    record('T');
    tasks++;
    CHECK(task == probe_worker && !strcmp(name, "usb_probe"));
    CHECK(stack == 3072 && !argument && priority == 3 && handle);
    if (create_result == pdPASS)
        *handle = &worker_token;
    return create_result;
}
void vTaskDelay(TickType_t ticks) {
    (void)ticks;
    CHECK(!"infinite worker is never run by the pump tests");
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) {
    (void)clear;
    (void)timeout;
    CHECK(!"infinite worker is never run by the pump tests");
    return 0;
}
int64_t esp_timer_get_time(void) {
    return 1000000;
}
static void stack_query(void) {
    CHECK(!in_isr && !callback_depth && !critical_depth && !isr_critical_depth);
    stack_queries++;
}
int tud_speed_get(void) {
    stack_query();
    return TUSB_SPEED_HIGH;
}
bool tud_inited(void) {
    stack_query();
    return driver_live;
}
bool tud_connected(void) {
    stack_query();
    return stack_connected;
}
bool tud_mounted(void) {
    stack_query();
    return stack_mounted;
}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config) {
    record('I');
    installs++;
    CHECK(config->event_cb == device_event && !config->event_arg);
    CHECK(config->descriptor.string_count == 5);
    CHECK(!strcmp(config->descriptor.string[1], "MyBot"));
    CHECK(!strcmp(config->descriptor.string[2], "MyBot S31 USB Probe"));
    driver_live = install_result == ESP_OK;
    return install_result;
}
esp_err_t tinyusb_driver_uninstall(void) {
    record('U');
    uninstalls++;
    CHECK(driver_live);
    if (cdc_live) {
        /* The USB worker may still dispatch a CDC callback while stopping. */
        cdcacm_event_t event = {.type = CDC_EVENT_RX};
        callback_depth++;
        cdc_event(PROBE_ITF, &event);
        callback_depth--;
    }
    if (uninstall_result == ESP_OK)
        driver_live = false;
    return uninstall_result;
}
esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config) {
    record('C');
    cdc_inits++;
    CHECK(config->cdc_port == PROBE_ITF);
    CHECK(config->callback_rx == cdc_event && config->callback_line_state_changed == cdc_event);
    CHECK(driver_live);
    cdc_live = cdc_result == ESP_OK;
    return cdc_result;
}
esp_err_t tinyusb_cdcacm_deinit(int interface) {
    record('D');
    cdc_deinits++;
    CHECK(interface == PROBE_ITF);
    CHECK(!driver_live && cdc_live); /* Never free the context while callbacks can run. */
    if (deinit_result == ESP_OK)
        cdc_live = false;
    return deinit_result;
}
bool tud_cdc_n_connected(uint8_t interface) {
    require_owner();
    CHECK(interface == PROBE_ITF);
    return connected;
}
void tud_cdc_n_read_flush(uint8_t interface) {
    require_owner();
    CHECK(interface == PROBE_ITF);
    rx_clears++;
    rx_length = 0;
}
void tud_cdc_n_write_clear(uint8_t interface) {
    require_owner();
    CHECK(interface == PROBE_ITF);
    tx_clears++;
    tx_length = 0;
}
esp_err_t tinyusb_cdcacm_read(int interface, uint8_t *buffer, size_t capacity, size_t *received) {
    require_owner();
    CHECK(interface == PROBE_ITF && buffer && received && capacity == PROBE_BUFFER_SIZE);
    reads++;
    if (read_result != ESP_OK)
        return read_result;
    if (bad_read_length) {
        *received = capacity + 1;
    } else {
        size_t count = rx_length;
        if (count > capacity)
            count = capacity;
        if (count > read_limit)
            count = read_limit;
        memcpy(buffer, rx_fifo, count);
        memmove(rx_fifo, rx_fifo + count, rx_length - count);
        rx_length -= count;
        *received = count;
    }
    if (after_read)
        after_read();
    return ESP_OK;
}
size_t tinyusb_cdcacm_write_queue(int interface, const uint8_t *buffer, size_t length) {
    require_owner();
    CHECK(interface == PROBE_ITF && buffer && length);
    writes++;
    if (bad_write_length)
        return length + 1;
    size_t count = length < write_limit ? length : write_limit;
    CHECK(tx_length + count <= sizeof(tx_fifo));
    memcpy(tx_fifo + tx_length, buffer, count);
    tx_length += count;
    return count;
}
esp_err_t tinyusb_cdcacm_write_flush(int interface, uint32_t timeout) {
    require_owner();
    CHECK(interface == PROBE_ITF && timeout == 0);
    flushes++;
    return flush_result;
}
static bool step(void) {
    CHECK(!owner && !callback_depth && !critical_depth);
    owner = true;
    bool progress = probe_step(&s_io);
    owner = false;
    CHECK(!critical_depth);
    return progress;
}
static void device(tinyusb_event_id_t type) {
    tinyusb_event_t event = {.id = type};
    callback_depth++;
    device_event(&event, NULL);
    callback_depth--;
}
static void line(int interface, bool dtr) {
    cdcacm_event_t event = {.type = CDC_EVENT_LINE_STATE_CHANGED,
                            .line_state_changed_data = {.dtr = dtr, .rts = true}};
    callback_depth++;
    cdc_event(interface, &event);
    callback_depth--;
}
static void rx_event(void) {
    cdcacm_event_t event = {.type = CDC_EVENT_RX};
    callback_depth++;
    cdc_event(PROBE_ITF, &event);
    callback_depth--;
}
static void input(const char *text) {
    size_t length = strlen(text);
    CHECK(rx_length + length <= sizeof(rx_fifo));
    memcpy(rx_fifo + rx_length, text, length);
    rx_length += length;
}
static void reset(void) {
    CHECK(!owner && !critical_depth && !callback_depth && !in_isr && !isr_critical_depth);
    memset(&s_bus, 0, sizeof(s_bus));
    memset(&s_link, 0, sizeof(s_link));
    memset(&s_io, 0, sizeof(s_io));
    memset(operations, 0, sizeof(operations));
    s_installed = s_start_attempted = false;
    reads = writes = flushes = rx_clears = tx_clears = notifications = 0;
    installs = cdc_inits = tasks = cdc_deinits = uninstalls = operation_count = 0;
    rx_length = tx_length = 0;
    connected = true;
    stack_connected = stack_mounted = false;
    task_lock_entries = isr_lock_entries = logs = controller_dumps = interrupt_dumps =
        stack_queries = 0;
    last_log[0] = 0;
    bad_read_length = bad_write_length = false;
    driver_live = cdc_live = false;
    read_limit = write_limit = SIZE_MAX;
    read_result = flush_result = install_result = cdc_result = deinit_result = uninstall_result =
        ESP_OK;
    create_result = pdPASS;
    after_read = NULL;
}
static void open_link(void) {
    device(TINYUSB_EVENT_ATTACHED);
    line(PROBE_ITF, true);
    CHECK(!reads && !writes && !rx_clears && !tx_clears);
    CHECK(!step()); /* Establish the epoch and clear inherited FIFOs before test input. */
    CHECK(rx_clears == 1 && tx_clears == 1);
    reads = flushes = 0;
}
static void test_callbacks_are_notifications_only(void) {
    reset();
    s_link.worker = &worker_token;
    device(TINYUSB_EVENT_ATTACHED);
    CHECK(s_link.mounted && !s_link.dtr && s_link.mounts == 1 && s_link.epoch == 1);
    line(PROBE_ITF, true);
    CHECK(s_link.dtr && s_link.epoch == 2);
    line(PROBE_ITF, true);
    CHECK(s_link.epoch == 2); /* Unchanged DTR cannot discard a live transfer. */
    line(PROBE_ITF + 1, false);
    CHECK(s_link.dtr && notifications == 3);
    rx_event();
    device(TINYUSB_EVENT_SUSPENDED);
    CHECK(s_link.suspended && s_link.suspends == 1 && s_link.epoch == 2);
    device(TINYUSB_EVENT_RESUMED);
    CHECK(!s_link.suspended && s_link.resumes == 1);
    device(TINYUSB_EVENT_DETACHED);
    CHECK(!s_link.mounted && !s_link.dtr && s_link.unmounts == 1 && s_link.epoch == 3);
    CHECK(notifications == 7 && !reads && !writes && !flushes && !rx_clears && !tx_clears);
    tests++;
}
static void test_short_read_write_and_backpressure(void) {
    reset();
    open_link();
    input("abcdefgh");
    read_limit = 3;
    write_limit = 1;
    CHECK(step() && reads == 1 && s_io.length == 3 && s_io.offset == 1);
    CHECK(s_io.rx_bytes == 3 && s_io.tx_queued == 1 && rx_length == 5);
    write_limit = 0;
    CHECK(!step() && !step());
    CHECK(reads == 1 && s_io.offset == 1 && s_io.backpressure == 2);
    write_limit = 2;
    CHECK(step() && reads == 1 && s_io.offset == 3);
    CHECK(step() && reads == 2 && s_io.length == 3 && s_io.offset == 2);
    CHECK(step() && reads == 2 && s_io.offset == 3);
    CHECK(step() && reads == 3 && s_io.length == 2 && s_io.offset == 2);
    CHECK(!step() && reads == 4 && !s_io.length && !s_io.offset);
    CHECK(tx_length == 8 && !memcmp(tx_fifo, "abcdefgh", 8));
    CHECK(s_io.rx_bytes == 8 && s_io.tx_queued == 8 && !s_io.io_errors);
    tests++;
}
static void test_not_finished_never_resends(void) {
    reset();
    open_link();
    input("once");
    flush_result = ESP_ERR_NOT_FINISHED;
    CHECK(step());
    CHECK(reads == 1 && writes == 1 && s_io.offset == 4 && s_io.tx_queued == 4);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(!step());
    CHECK(writes == 1 && tx_length == 4 && !memcmp(tx_fifo, "once", 4));
    CHECK(!s_io.io_errors && flushes == 5);
    flush_result = ESP_OK;
    input("more");
    CHECK(step() && writes == 2 && tx_length == 8 && !memcmp(tx_fifo, "oncemore", 8));
    CHECK(s_io.rx_bytes == 8 && s_io.tx_queued == 8);
    tests++;
}
static void test_closed_links_clear_pending(void) {
    for (unsigned event = 0; event < 3; ++event) {
        reset();
        open_link();
        input("old-data");
        write_limit = 2;
        CHECK(step() && s_io.offset == 2 && s_io.length == 8);
        unsigned previous_reads = reads, previous_writes = writes;
        if (event == 0)
            line(PROBE_ITF, false);
        else if (event == 1)
            device(TINYUSB_EVENT_DETACHED);
        else
            device(TINYUSB_EVENT_ATTACHED);
        CHECK(s_io.offset == 2 && s_io.length == 8); /* Callback itself never clears I/O state. */
        CHECK(!step());
        CHECK(!s_io.offset && !s_io.length && reads == previous_reads && writes == previous_writes);
        CHECK(rx_clears == 2 && tx_clears == 2 && !tx_length);
        device(TINYUSB_EVENT_ATTACHED);
        line(PROBE_ITF, true);
        CHECK(!step());
        input("new");
        write_limit = SIZE_MAX;
        CHECK(step() && tx_length == 3 && !memcmp(tx_fifo, "new", 3));
        tests++;
    }
}
static void detach_during_read(void) {
    device(TINYUSB_EVENT_DETACHED);
}
static void close_during_read(void) {
    line(PROBE_ITF, false);
}
static void test_epoch_changes_during_read(void) {
    for (unsigned event = 0; event < 2; ++event) {
        reset();
        open_link();
        input("stale");
        after_read = event ? close_during_read : detach_during_read;
        CHECK(!step());
        CHECK(reads == 1 && !writes && !flushes && s_io.rx_bytes == 5);
        after_read = NULL;
        CHECK(!step() && !s_io.length && !s_io.offset && rx_clears == 2 && tx_clears == 2);
        device(TINYUSB_EVENT_ATTACHED);
        line(PROBE_ITF, true);
        CHECK(!step());
        input("fresh");
        CHECK(step() && tx_length == 5 && !memcmp(tx_fifo, "fresh", 5));
        tests++;
    }
}
static void test_suspend_and_connection_gate(void) {
    reset();
    open_link();
    input("abcdef");
    write_limit = 2;
    CHECK(step());
    device(TINYUSB_EVENT_SUSPENDED);
    unsigned previous_reads = reads, previous_writes = writes, previous_flushes = flushes;
    CHECK(!step() && s_io.offset == 2 && s_io.length == 6);
    CHECK(reads == previous_reads && writes == previous_writes && flushes == previous_flushes);
    CHECK(rx_clears == 1 && tx_clears == 1);
    device(TINYUSB_EVENT_RESUMED);
    connected = false;
    CHECK(!step() && writes == previous_writes);
    connected = true;
    write_limit = SIZE_MAX;
    CHECK(step() && reads == previous_reads && s_io.offset == 6);
    CHECK(tx_length == 6 && !memcmp(tx_fifo, "abcdef", 6));
    tests++;
}
static void test_api_errors_are_bounded(void) {
    for (unsigned fault = 0; fault < 4; ++fault) {
        reset();
        open_link();
        input("data");
        if (fault == 0)
            read_result = ESP_FAIL;
        else if (fault == 1)
            bad_read_length = true;
        else if (fault == 2)
            bad_write_length = true;
        else
            flush_result = ESP_FAIL;
        CHECK(step() == (fault == 3));
        CHECK(s_io.io_errors == 1 && reads == 1 && writes <= 1 && flushes <= 1);
        if (fault != 3)
            CHECK(!s_io.length && !s_io.offset);
        else {
            CHECK(s_io.offset == 4 && s_io.tx_queued == 4);
            CHECK(!step() && writes == 1 && s_io.io_errors == 2);
        }
        tests++;
    }
}
static void test_init_failure_and_cleanup(void) {
    for (unsigned fault = 0; fault < 6; ++fault) {
        reset();
        bool worker_failure = fault == 2 || fault == 4 || fault == 5;
        if (fault == 0)
            install_result = ESP_FAIL;
        else if (fault == 1 || fault == 3)
            cdc_result = ESP_FAIL;
        else
            create_result = 0;
        if (fault == 3 || fault == 4)
            uninstall_result = ESP_FAIL;
        if (fault == 5)
            deinit_result = ESP_FAIL;
        esp_err_t expected = worker_failure ? ESP_ERR_NO_MEM : ESP_FAIL;
        CHECK(mybot_usb_cdc_probe_start() == expected);
        CHECK(!s_installed && s_start_attempted && !s_link.worker);
        CHECK(!interrupt_dumps && !controller_dumps);
        CHECK(installs == 1 && cdc_inits == (fault == 0 ? 0U : 1U));
        CHECK(tasks == (worker_failure ? 1U : 0U));
        CHECK(cdc_deinits == (fault == 2 || fault == 5 ? 1U : 0U));
        CHECK(uninstalls == (fault == 0 ? 0U : 1U));
        const char *expected_order = fault == 0                 ? "I"
                                     : fault == 1 || fault == 3 ? "ICU"
                                     : fault == 4               ? "ICTU"
                                                                : "ICTUD";
        CHECK(!strcmp(operations, expected_order));
        CHECK(driver_live == (fault == 3 || fault == 4));
        CHECK(cdc_live == (fault == 4 || fault == 5));
        unsigned previous_operations = operation_count;
        CHECK(mybot_usb_cdc_probe_start() == ESP_ERR_INVALID_STATE);
        CHECK(installs == 1 && operation_count == previous_operations);
        tests++;
    }
}
static void test_successful_start_is_idempotent(void) {
    reset();
    CHECK(mybot_usb_cdc_probe_start() == ESP_OK);
    CHECK(s_installed && s_start_attempted && s_link.worker == &worker_token);
    CHECK(!strcmp(operations, "ICT") && installs == 1 && cdc_inits == 1 && tasks == 1);
    CHECK(interrupt_dumps == 1 && !controller_dumps);
    CHECK(mybot_usb_cdc_probe_start() == ESP_OK && !strcmp(operations, "ICT"));
    CHECK(interrupt_dumps == 1 && !controller_dumps);
    CHECK(!cdc_deinits && !uninstalls);
    rx_event();
    CHECK(notifications == 1 && !reads && !writes && !flushes);
    tests++;
}
static void hook(uint8_t port, uint32_t event, bool isr) {
    CHECK(!in_isr && !callback_depth);
    in_isr = isr;
    callback_depth++;
    tud_event_hook_cb(port, event, isr);
    callback_depth--;
    in_isr = false;
    CHECK(!critical_depth && !isr_critical_depth);
}
static void test_bus_hook_context_and_mapping(void) {
    reset();
    s_link.worker = &worker_token;
    probe_link_t original_link = s_link;
    probe_io_t original_io = s_io;
    const uint32_t events[] = {DCD_EVENT_BUS_RESET, DCD_EVENT_SETUP_RECEIVED,
                               DCD_EVENT_XFER_COMPLETE, DCD_EVENT_UNPLUGGED};
    uint32_t expected[4] = {0};
    for (unsigned context = 0; context < 2; ++context) {
        for (unsigned event = 0; event < 4; ++event) {
            for (unsigned occurrence = 0; occurrence <= event; ++occurrence) {
                hook(0, events[event], context != 0);
                expected[event]++;
                CHECK(s_bus.resets == expected[0] && s_bus.setups == expected[1] &&
                      s_bus.transfers == expected[2] && s_bus.unplugs == expected[3]);
            }
        }
    }
    CHECK(task_lock_entries == 10 && isr_lock_entries == 10);
    CHECK(!logs && !notifications && !reads && !writes && !flushes && !rx_clears && !tx_clears);
    CHECK(!controller_dumps && !interrupt_dumps && !stack_queries);
    CHECK(!memcmp(&original_link, &s_link, sizeof(s_link)) &&
          !memcmp(&original_io, &s_io, sizeof(s_io)));
    probe_bus_t copied = bus_snapshot();
    CHECK(copied.resets == 2 && copied.setups == 4 && copied.transfers == 6 && copied.unplugs == 8);
    copied.resets = 10;
    CHECK(s_bus.resets == 2);
    tests++;
}
static void test_bus_hook_foreign_port_and_unknown_event(void) {
    reset();
    hook(1, DCD_EVENT_BUS_RESET, false);
    hook(1, DCD_EVENT_SETUP_RECEIVED, true);
    hook(255, DCD_EVENT_XFER_COMPLETE, true);
    CHECK(!task_lock_entries && !isr_lock_entries);
    hook(0, DCD_EVENT_INVALID, false);
    hook(0, DCD_EVENT_SOF, true);
    hook(0, DCD_EVENT_SUSPEND, true);
    hook(0, UINT32_MAX, false);
    CHECK(task_lock_entries == 2 && isr_lock_entries == 2);
    CHECK(!s_bus.resets && !s_bus.setups && !s_bus.transfers && !s_bus.unplugs);
    CHECK(!logs && !notifications && !reads && !writes && !flushes && !stack_queries);
    s_bus.resets = UINT32_MAX;
    hook(0, DCD_EVENT_BUS_RESET, true);
    CHECK(s_bus.resets == 0 && !s_bus.setups); /* Counter wrap remains defined and local. */
    tests++;
}
static void test_status_diagnostics_stay_in_task_context(void) {
    reset();
    CHECK(mybot_usb_cdc_probe_start() == ESP_OK);
    CHECK(interrupt_dumps == 1 && !controller_dumps);
    hook(0, DCD_EVENT_BUS_RESET, true);
    hook(0, DCD_EVENT_SETUP_RECEIVED, true);
    hook(0, DCD_EVENT_XFER_COMPLETE, false);
    hook(0, DCD_EVENT_UNPLUGGED, false);
    unsigned previous_logs = logs;
    log_probe_status(&s_link, &s_io);
    CHECK(controller_dumps == 1 && interrupt_dumps == 1 && logs == previous_logs + 1);
    CHECK(strstr(last_log, "mounted=0 dtr=0 suspended=0 speed=none"));
    CHECK(strstr(last_log, "stack_inited=1 stack_connected=0 stack_mounted=0"));
    CHECK(strstr(last_log, "reset_events=1 setup_events=1 xfer_events=1 unplug_events=1"));
    stack_connected = true;
    stack_mounted = true;
    /* A missed early application mount callback must remain distinguishable
     * from the stack itself never completing enumeration. */
    log_probe_status(&s_link, &s_io);
    CHECK(controller_dumps == 2 && interrupt_dumps == 1);
    CHECK(strstr(last_log, "mounted=0 dtr=0 suspended=0 speed=high"));
    CHECK(strstr(last_log, "stack_inited=1 stack_connected=1 stack_mounted=1"));
    CHECK(!reads && !writes && !flushes && !rx_clears && !tx_clears && !notifications);
    tests++;
}

int main(void) {
    test_bus_hook_context_and_mapping();
    test_bus_hook_foreign_port_and_unknown_event();
    test_status_diagnostics_stay_in_task_context();
    test_callbacks_are_notifications_only();
    test_short_read_write_and_backpressure();
    test_not_finished_never_resends();
    test_closed_links_clear_pending();
    test_epoch_changes_during_read();
    test_suspend_and_connection_gate();
    test_api_errors_are_bounded();
    test_init_failure_and_cleanup();
    test_successful_start_is_idempotent();
    printf("USB CDC probe: %u production pump/init scenarios, %u checks passed (UBSan)\n", tests,
           checks);
    return 0;
}
