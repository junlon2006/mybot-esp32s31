/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_cdc_probe_diag.h"

#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "hal/usb_dwc_ll.h"
#include "hal/usb_utmi_ll.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "usb_cdc_probe";

void mybot_usb_cdc_probe_log_controller(void) {
    const bool clocks_enabled = _usb_utmi_ll_bus_clock_is_enabled();
    const hp_alive_sys_usb_otghs_ctrl_reg_t phy_control = {
        .val = HP_ALIVE_SYS.usb_otghs_ctrl.val,
    };
    const unsigned dp_pulldown = HP_ALIVE_SYS.usb_ctrl.usb_otghs_phy_dppulldown;
    const unsigned dm_pulldown = HP_ALIVE_SYS.usb_ctrl.usb_otghs_phy_dmpulldown;

    ESP_LOGI(TAG,
             "event=usb_probe_phy clocks_enabled=%u suspendm_force=%u pll_force=%u "
             "otg_suspendm=%u dp_pulldown=%u dm_pulldown=%u control=0x%08" PRIx32,
             (unsigned)clocks_enabled, (unsigned)phy_control.reg_usb_otghs_phy_suspendm_force_en,
             (unsigned)phy_control.reg_usb_otghs_phy_pll_force_en,
             (unsigned)phy_control.reg_usb_otghs_phy_otg_suspendm, dp_pulldown, dm_pulldown,
             phy_control.val);
    /* Avoid controller register accesses if its required clocks are gated. */
    if (!clocks_enabled) {
        ESP_LOGW(TAG, "event=usb_probe_controller snapshot=skipped reason=clock_disabled");
        return;
    }

    usb_dwc_dev_t *const hw = USB_DWC_LL_GET_HW(0);
    /* Only ordinary control and read-only status registers are sampled. Do not
     * add FIFO/pop, interrupt-status (W1C), reset or write-based HAL helpers. */
    const uint32_t core_id = hw->gsnpsid_reg.val;
    const uint32_t hardware_config2 = hw->ghwcfg2_reg.val;
    const uint32_t hardware_config4 = hw->ghwcfg4_reg.val;
    const usb_dwc_gotgctl_reg_t session = {.val = hw->gotgctl_reg.val};
    const usb_dwc_gusbcfg_reg_t usb_config = {.val = hw->gusbcfg_reg.val};
    const usb_dwc_gahbcfg_reg_t bus_config = {.val = hw->gahbcfg_reg.val};
    const uint32_t interrupt_mask = hw->gintmsk_reg.val;
    const usb_dwc_dcfg_reg_t device_config = {.val = hw->dcfg_reg.val};
    const usb_dwc_dctl_reg_t device_control = {.val = hw->dctl_reg.val};
    const usb_dwc_dsts_reg_t device_status = {.val = hw->dsts_reg.val};
    const usb_dwc_pcgcctl_reg_t power = {.val = hw->pcgcctl_reg.val};

    /* B-session valid may be supplied internally by this controller, so it is
     * not a measurement of connector VBUS. enumspd is deliberately left raw:
     * its reset value alone does not establish successful HS enumeration. */
    ESP_LOGI(TAG,
             "event=usb_probe_controller mode=%s force_device=%u force_host=%u "
             "soft_disconnect=%u address=%u b_session_valid=%u b_override_en=%u "
             "b_override_value=%u vbus_override_en=%u vbus_override_value=%u "
             "frame=%u line_state=%u suspended=%u enum_speed_raw=%u stop_pclk=%u "
             "gate_hclk=%u phy_sleep=%u global_irq_enabled=%u dma_enabled=%u "
             "phy_sel=%u phy_16bit=%u",
             session.curmod ? "host" : "device", (unsigned)usb_config.forcedevmode,
             (unsigned)usb_config.forcehstmode, (unsigned)device_control.sftdiscon,
             (unsigned)device_config.devaddr, (unsigned)session.bsesvld,
             (unsigned)session.bvalidoven, (unsigned)session.bvalidovval,
             (unsigned)session.vbvalidoven, (unsigned)session.vbvalidovval,
             (unsigned)device_status.soffn, (unsigned)device_status.devlnsts,
             (unsigned)device_status.suspsts, (unsigned)device_status.enumspd,
             (unsigned)power.stoppclk, (unsigned)power.gatehclk, (unsigned)power.physleep,
             (unsigned)bus_config.glbllntrmsk, (unsigned)bus_config.dmaen,
             (unsigned)usb_config.physel, (unsigned)usb_config.phyif);
    ESP_LOGI(TAG,
             "event=usb_probe_registers core_id=0x%08" PRIx32 " ghwcfg2=0x%08" PRIx32
             " ghwcfg4=0x%08" PRIx32 " gotgctl=0x%08" PRIx32 " gusbcfg=0x%08" PRIx32
             " gahbcfg=0x%08" PRIx32 " gintmsk=0x%08" PRIx32 " dcfg=0x%08" PRIx32
             " dctl=0x%08" PRIx32 " dsts=0x%08" PRIx32 " pcgcctl=0x%08" PRIx32,
             core_id, hardware_config2, hardware_config4, session.val, usb_config.val,
             bus_config.val, interrupt_mask, device_config.val, device_control.val,
             device_status.val, power.val);
}

void mybot_usb_cdc_probe_log_interrupts(void) {
    static bool dumped;
    if (dumped)
        return;
    dumped = true;
    ESP_LOGI(TAG, "event=usb_probe_interrupts phase=begin");
    esp_err_t result = esp_intr_dump(stdout);
    ESP_LOGI(TAG, "event=usb_probe_interrupts phase=end result=%s", esp_err_to_name(result));
}
