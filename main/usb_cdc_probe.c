/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_cdc_probe.h"
#include "usb_cdc_probe_diag.h"

#include "device/dcd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#define PROBE_ITF TINYUSB_CDC_ACM_0
#define PROBE_BUFFER_SIZE 512U
#define PROBE_WORKER_STACK 3072U

static const char *TAG = "usb_cdc_probe";
static const char *s_strings[] = {
    (const char[]){0x09, 0x04}, "MyBot", "MyBot S31 USB Probe", "S31-CDC-PROBE",
    "Native USB CDC echo",
};

typedef struct {
    uint32_t epoch;
    uint32_t mounts;
    uint32_t unmounts;
    uint32_t suspends;
    uint32_t resumes;
    bool mounted;
    bool suspended;
    bool dtr;
    TaskHandle_t worker;
} probe_link_t;

typedef struct {
    uint8_t pending[PROBE_BUFFER_SIZE];
    size_t length;
    size_t offset;
    uint32_t epoch;
    uint64_t rx_bytes;
    uint64_t tx_queued;
    uint32_t backpressure;
    uint32_t io_errors;
} probe_io_t;

typedef struct {
    uint32_t resets;
    uint32_t setups;
    uint32_t transfers;
    uint32_t unplugs;
} probe_bus_t;

static portMUX_TYPE s_link_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_bus_lock = portMUX_INITIALIZER_UNLOCKED;
static probe_link_t s_link;
static probe_bus_t s_bus;
static probe_io_t s_io;
static bool s_installed;
static bool s_start_attempted;

/* Public TinyUSB hook: it can run in the USB ISR before the device task sees
 * SET_CONFIGURATION. Keep it to counters under the correct critical primitive;
 * no logging, allocation, FIFO access or task notification belongs here. */
void tud_event_hook_cb(uint8_t rhport, uint32_t event_id, bool in_isr) {
    if (rhport != 0)
        return;
    if (in_isr)
        portENTER_CRITICAL_ISR(&s_bus_lock);
    else
        portENTER_CRITICAL(&s_bus_lock);
    switch (event_id) {
    case DCD_EVENT_BUS_RESET:
        s_bus.resets++;
        break;
    case DCD_EVENT_SETUP_RECEIVED:
        s_bus.setups++;
        break;
    case DCD_EVENT_XFER_COMPLETE:
        s_bus.transfers++;
        break;
    case DCD_EVENT_UNPLUGGED:
        s_bus.unplugs++;
        break;
    default:
        break;
    }
    if (in_isr)
        portEXIT_CRITICAL_ISR(&s_bus_lock);
    else
        portEXIT_CRITICAL(&s_bus_lock);
}

static probe_bus_t bus_snapshot(void) {
    portENTER_CRITICAL(&s_bus_lock);
    probe_bus_t bus = s_bus;
    portEXIT_CRITICAL(&s_bus_lock);
    return bus;
}

static probe_link_t link_snapshot(void) {
    portENTER_CRITICAL(&s_link_lock);
    probe_link_t link = s_link;
    portEXIT_CRITICAL(&s_link_lock);
    return link;
}

static void notify_worker(void) {
    probe_link_t link = link_snapshot();
    if (link.worker)
        xTaskNotifyGive(link.worker);
}

/* TinyUSB's task invokes these callbacks; only small link state is shared.
 * Application read/write/FIFO clearing belongs exclusively to probe_worker. */
static void device_event(tinyusb_event_t *event, void *arg) {
    (void)arg;
    portENTER_CRITICAL(&s_link_lock);
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_link.mounts++;
        s_link.epoch++;
        s_link.mounted = true;
        s_link.suspended = false;
        s_link.dtr = false;
        break;
    case TINYUSB_EVENT_DETACHED:
        s_link.unmounts++;
        s_link.epoch++;
        s_link.mounted = s_link.dtr = false;
        break;
#if CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        s_link.suspends++;
        s_link.suspended = true;
        break;
#endif
#if CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        s_link.resumes++;
        s_link.suspended = false;
        break;
#endif
    default:
        break;
    }
    portEXIT_CRITICAL(&s_link_lock);
    notify_worker();
}

static void cdc_event(int itf, cdcacm_event_t *event) {
    if (itf != PROBE_ITF)
        return;
    if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        portENTER_CRITICAL(&s_link_lock);
        bool dtr = event->line_state_changed_data.dtr;
        if (dtr != s_link.dtr) {
            s_link.epoch++;
            s_link.dtr = dtr;
        }
        portEXIT_CRITICAL(&s_link_lock);
    }
    notify_worker();
}

static bool link_open(const probe_link_t *link) {
    return link->mounted && !link->suspended && link->dtr && tud_cdc_n_connected(PROBE_ITF);
}

/* One bounded iteration. A short/zero TX write keeps the unsent suffix, and
 * stops draining RX until space is available. FIFO clear cannot retract bytes
 * already handed to a USB endpoint or received by the host. */
static bool probe_step(probe_io_t *io) {
    probe_link_t link = link_snapshot();
    if (io->epoch != link.epoch) {
        io->length = io->offset = 0;
        io->epoch = link.epoch;
        tud_cdc_n_read_flush(PROBE_ITF);
        tud_cdc_n_write_clear(PROBE_ITF);
    }
    if (!link_open(&link))
        return false;
    if (io->offset == io->length) {
        io->length = io->offset = 0;
        size_t received = 0;
        if (tinyusb_cdcacm_read(PROBE_ITF, io->pending, sizeof(io->pending), &received) != ESP_OK ||
            received > sizeof(io->pending)) {
            io->io_errors++;
            return false;
        }
        io->length = received;
        io->rx_bytes += received;
    }
    probe_link_t current = link_snapshot();
    if (current.epoch != link.epoch || !link_open(&current))
        return false;

    size_t queued = 0;
    if (io->offset < io->length) {
        size_t remaining = io->length - io->offset;
        queued = tinyusb_cdcacm_write_queue(PROBE_ITF, io->pending + io->offset, remaining);
        if (queued > remaining) {
            io->io_errors++;
            io->length = io->offset = 0;
            return false;
        }
        io->offset += queued;
        io->tx_queued += queued;
        if (!queued)
            io->backpressure++;
    }
    /* ESP_ERR_NOT_FINISHED is normal backpressure, never a reason to resend. */
    esp_err_t result = tinyusb_cdcacm_write_flush(PROBE_ITF, 0);
    if (result != ESP_OK && result != ESP_ERR_NOT_FINISHED)
        io->io_errors++;
    return queued != 0;
}

static void log_probe_status(const probe_link_t *link, const probe_io_t *io) {
    probe_bus_t bus = bus_snapshot();
    ESP_LOGI(TAG,
             "event=usb_probe mounted=%d dtr=%d suspended=%d speed=%s "
             "stack_inited=%d stack_connected=%d stack_mounted=%d "
             "reset_events=%" PRIu32 " setup_events=%" PRIu32 " xfer_events=%" PRIu32
             " unplug_events=%" PRIu32 " mounts=%" PRIu32 " unmounts=%" PRIu32 " suspends=%" PRIu32
             " resumes=%" PRIu32 " rx_bytes=%" PRIu64 " tx_queued=%" PRIu64 " backpressure=%" PRIu32
             " io_errors=%" PRIu32,
             link->mounted, link->dtr, link->suspended,
             !tud_connected()                     ? "none"
             : tud_speed_get() == TUSB_SPEED_HIGH ? "high"
                                                  : "full",
             tud_inited(), tud_connected(), tud_mounted(), bus.resets, bus.setups, bus.transfers,
             bus.unplugs, link->mounts, link->unmounts, link->suspends, link->resumes, io->rx_bytes,
             io->tx_queued, io->backpressure, io->io_errors);
    mybot_usb_cdc_probe_log_controller();
}

static void probe_worker(void *arg) {
    (void)arg;
    int64_t reported_at = 0;
    uint32_t reported_epoch = UINT32_MAX;
    uint32_t reported_suspends = 0, reported_resumes = 0;
    s_io.epoch = UINT32_MAX;
    for (;;) {
        bool progress = probe_step(&s_io);
        probe_link_t link = link_snapshot();
        int64_t now = esp_timer_get_time();
        if (link.epoch != reported_epoch || link.suspends != reported_suspends ||
            link.resumes != reported_resumes || now - reported_at >= 5000000) {
            log_probe_status(&link, &s_io);
            reported_epoch = link.epoch;
            reported_suspends = link.suspends;
            reported_resumes = link.resumes;
            reported_at = now;
        }
        if (progress)
            vTaskDelay(1);
        else
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
}

esp_err_t mybot_usb_cdc_probe_start(void) {
    if (s_installed)
        return ESP_OK;
    /* A failed vendor teardown may retain tasks/PHY state. The diagnostic has
     * one startup attempt per boot, never retries against partial ownership. */
    if (s_start_attempted)
        return ESP_ERR_INVALID_STATE;
    s_start_attempted = true;
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(device_event, NULL);
    config.descriptor.string = s_strings;
    config.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    /* S31 has one HS-capable port (index 0). Target defaults select UTMI and
     * the correct controller; the enum's historical FULL_SPEED_0 name is not
     * a request to use an S3-style FS PHY. No board VBUS-control GPIO exists.
     * phy.self_powered=false disables VBUS monitoring, not the physical 5 V. */
    ESP_LOGW(TAG, "event=usb_probe vbus_control=unavailable vbus_monitor=unavailable");
    esp_err_t result = tinyusb_driver_install(&config);
    if (result != ESP_OK)
        return result;
    tinyusb_config_cdcacm_t cdc = {
        .cdc_port = PROBE_ITF,
        .callback_rx = cdc_event,
        .callback_line_state_changed = cdc_event,
    };
    result = tinyusb_cdcacm_init(&cdc);
    if (result != ESP_OK) {
        if (tinyusb_driver_uninstall() != ESP_OK)
            ESP_LOGE(TAG, "event=usb_probe cleanup=driver_failed recovery=reboot");
        return result;
    }
    TaskHandle_t worker = NULL;
    if (xTaskCreate(probe_worker, "usb_probe", PROBE_WORKER_STACK, NULL, 3, &worker) != pdPASS) {
        /* Stop the USB task before freeing the CDC callback context. If the
         * task cannot stop, retain CDC ownership for the rest of this boot. */
        if (tinyusb_driver_uninstall() != ESP_OK) {
            ESP_LOGE(TAG, "event=usb_probe cleanup=driver_failed recovery=reboot");
        } else if (tinyusb_cdcacm_deinit(PROBE_ITF) != ESP_OK) {
            ESP_LOGE(TAG, "event=usb_probe cleanup=cdc_failed recovery=reboot");
        }
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_link_lock);
    s_link.worker = worker;
    portEXIT_CRITICAL(&s_link_lock);
    s_installed = true;
    ESP_LOGI(TAG, "event=usb_probe state=initialized product=MyBot_S31_USB_Probe class=CDC diag=2");
    mybot_usb_cdc_probe_log_interrupts();
    return ESP_OK;
}
