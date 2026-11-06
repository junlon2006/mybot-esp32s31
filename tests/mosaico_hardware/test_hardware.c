/* SPDX-License-Identifier: Apache-2.0 */
#include "host_hardware.h"
#include "mosaico_hardware.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

struct host_i2c_bus {
    bool open;
} s_bus;
static uint16_t s_version = 0x0102;
static unsigned s_efuse_reads, s_gpio_calls, s_register_reads, s_i2c_calls;
static unsigned s_timer_calls, s_channel_calls, s_install_calls, s_uninstall_calls, s_stop_calls;
static unsigned s_fade_calls, s_wait_calls, s_settle_calls;
static uint32_t s_enable, s_output;
static int s_levels[64], s_modes[64];
static bool s_timer, s_channel, s_fade_service, s_shared_fade, s_failed;
static bool s_degraded, s_compatible;
static const char *s_failure;
static char s_events[128][32];
static unsigned s_event_count;
static const esp_efuse_desc_t s_user_data = {1};
const esp_efuse_desc_t *ESP_EFUSE_USER_DATA[] = {&s_user_data, NULL};

static void event(const char *name) {
    CHECK(s_event_count < 128);
    snprintf(s_events[s_event_count++], 32, "%s", name);
}
static bool fail(const char *name) {
    if (!s_failed && (!strcmp(s_failure, name) ||
                      (!strcmp(s_failure, "fade_shared_failure") && !strcmp(name, "fade_set")))) {
        s_failed = true;
        return true;
    }
    return false;
}
static unsigned position(const char *name) {
    for (unsigned i = 0; i < s_event_count; ++i)
        if (!strcmp(s_events[i], name))
            return i;
    CHECK(false);
    return 0;
}
const char *esp_err_to_name(esp_err_t result) {
    return result == ESP_OK ? "OK" : "FAIL";
}
void hardware_log(const char *level, const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (!strcmp(level, "warning") && strstr(text, "result=degraded") &&
        strstr(text, "assume_v1.2_pin_map"))
        s_degraded = true;
    if (strstr(text, "compatible_v1.1"))
        s_compatible = true;
}
esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t *field[], void *out, size_t bits) {
    CHECK(field == ESP_EFUSE_USER_DATA && out && bits == 16);
    ++s_efuse_reads;
    event("efuse");
    if (fail("efuse"))
        return ESP_FAIL;
    memcpy(out, &s_version, sizeof(s_version));
    return ESP_OK;
}
uint32_t hardware_register_read(uint32_t address) {
    CHECK(s_efuse_reads);
    ++s_register_reads;
    if (address == GPIO_ENABLE1_REG)
        return s_enable;
    CHECK(address == GPIO_OUT1_REG);
    return s_output;
}
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
    CHECK(s_efuse_reads && (pin == 60 || pin == 57 || pin == 45) && level <= 1);
    ++s_gpio_calls;
    if ((pin == 60 && !s_timer_calls && fail("gpio_vcc")) || (pin == 57 && fail("gpio_shutdown")) ||
        (pin == 45 && fail("gpio_pa")) || (pin == 60 && s_stop_calls && fail("restore")))
        return ESP_FAIL;
    s_levels[pin] = (int)level;
    if (pin == 60) {
        if (level)
            s_output |= BIT(28);
        else
            s_output &= ~BIT(28);
        event(level ? "vcc_off" : "vcc_on");
    } else {
        event(pin == 57 ? "shutdown_released" : "pa_off");
    }
    return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *config) {
    CHECK(config && !config->pull_up_en && !config->pull_down_en && !config->intr_type);
    int pin = config->pin_bit_mask == (UINT64_C(1) << 60)   ? 60
              : config->pin_bit_mask == (UINT64_C(1) << 57) ? 57
                                                            : 45;
    CHECK(config->pin_bit_mask == (UINT64_C(1) << pin));
    CHECK(s_levels[pin] >= 0);
    CHECK(config->mode == (pin == 57 ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT));
    ++s_gpio_calls;
    if (fail("gpio_config"))
        return ESP_FAIL;
    s_modes[pin] = config->mode;
    if (pin == 60)
        s_enable |= BIT(28);
    return ESP_OK;
}
esp_err_t gpio_hold_dis(gpio_num_t pin) {
    CHECK((pin == 60 || pin == 57 || pin == 45) && s_modes[pin] >= 0);
    ++s_gpio_calls;
    return fail("hold") ? ESP_FAIL : ESP_OK;
}
esp_err_t ledc_timer_config(const ledc_timer_config_t *config) {
    CHECK(config && config->speed_mode == LEDC_LOW_SPEED_MODE &&
          config->duty_resolution == LEDC_TIMER_8_BIT && config->timer_num == LEDC_TIMER_1 &&
          config->freq_hz == 100000 && config->clk_cfg == LEDC_AUTO_CLK);
    CHECK(s_levels[60] == 1 && s_modes[60] == GPIO_MODE_OUTPUT && s_levels[57] == 1 &&
          s_modes[57] == GPIO_MODE_OUTPUT_OD && s_levels[45] == 0);
    ++s_timer_calls;
    event("timer");
    if (fail("timer"))
        return ESP_FAIL;
    s_timer = true;
    return ESP_OK;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *config) {
    CHECK(s_timer && config && config->gpio_num == 60 &&
          config->speed_mode == LEDC_LOW_SPEED_MODE && config->channel == LEDC_CHANNEL_1 &&
          config->intr_type == LEDC_INTR_DISABLE && config->timer_sel == LEDC_TIMER_1 &&
          config->duty == 255 && !config->hpoint);
    ++s_channel_calls;
    event("channel");
    if (fail("channel"))
        return ESP_FAIL;
    s_channel = true;
    return ESP_OK;
}
esp_err_t ledc_fade_func_install(int flags) {
    CHECK(!flags && s_channel);
    ++s_install_calls;
    event("install");
    if (s_shared_fade) {
        CHECK(s_fade_service);
        return ESP_ERR_INVALID_STATE;
    }
    if (fail("fade_install"))
        return ESP_FAIL;
    CHECK(!s_fade_service);
    s_fade_service = true;
    return ESP_OK;
}
void ledc_fade_func_uninstall(void) {
    CHECK(s_fade_service && !s_shared_fade);
    ++s_uninstall_calls;
    event("uninstall");
    s_fade_service = false;
}
esp_err_t ledc_set_fade_with_time(ledc_mode_t mode, ledc_channel_t channel, uint32_t duty, int ms) {
    CHECK(s_fade_service && mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_1 && duty == 0 &&
          ms == 50);
    ++s_fade_calls;
    event("fade_255_to_0");
    return fail("fade_set") ? ESP_FAIL : ESP_OK;
}
esp_err_t ledc_fade_start(ledc_mode_t mode, ledc_channel_t channel, ledc_fade_mode_t wait) {
    CHECK(s_fade_service && s_fade_calls && mode == LEDC_LOW_SPEED_MODE &&
          channel == LEDC_CHANNEL_1 && wait == LEDC_FADE_WAIT_DONE);
    ++s_wait_calls;
    event("wait_done");
    return fail("fade_start") ? ESP_FAIL : ESP_OK;
}
esp_err_t ledc_stop(ledc_mode_t mode, ledc_channel_t channel, uint32_t idle_level) {
    CHECK(s_timer && mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_1 && idle_level <= 1);
    ++s_stop_calls;
    event(idle_level ? "stop_off" : "stop_on");
    return fail("stop") ? ESP_FAIL : ESP_OK;
}
void vTaskDelay(TickType_t ticks) {
    CHECK(ticks == 10 && s_levels[60] == 0 && s_modes[60] == GPIO_MODE_OUTPUT);
    ++s_settle_calls;
    event("settled");
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *out) {
    CHECK(config && out && !s_bus.open && config->i2c_port == I2C_NUM_0 &&
          config->sda_io_num == 56 && config->scl_io_num == 3 &&
          config->clk_source == I2C_CLK_SRC_DEFAULT && config->glitch_ignore_cnt == 7 &&
          config->flags.enable_internal_pullup);
    CHECK(s_levels[60] == 0 && s_modes[60] == GPIO_MODE_OUTPUT);
    CHECK(s_levels[57] == 1 && s_modes[57] == GPIO_MODE_OUTPUT_OD && s_levels[45] == 0);
    CHECK(s_settle_calls == 1);
    ++s_i2c_calls;
    event("i2c");
    if (fail("i2c"))
        return ESP_FAIL;
    s_bus.open = true;
    *out = &s_bus;
    return ESP_OK;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    s_failure = argv[1];
    for (unsigned i = 0; i < 64; ++i)
        s_levels[i] = s_modes[i] = -1;
    if (!strcmp(s_failure, "revision_10"))
        s_version = 0x0100;
    else if (!strcmp(s_failure, "revision_11"))
        s_version = 0x0101;
    else if (!strcmp(s_failure, "blank"))
        s_version = 0;
    else if (!strcmp(s_failure, "unknown"))
        s_version = 0x0200;
    if (!strcmp(s_failure, "adopt_on") || !strcmp(s_failure, "adopt_off")) {
        s_enable = BIT(28);
        s_output = !strcmp(s_failure, "adopt_off") ? BIT(28) : 0;
    }
    s_shared_fade = !strcmp(s_failure, "fade_shared") || !strcmp(s_failure, "fade_shared_failure");
    s_fade_service = s_shared_fade;
    bool rejected = !strcmp(s_failure, "revision_10") || !strcmp(s_failure, "efuse");
    bool normal = !strcmp(s_failure, "normal") || !strcmp(s_failure, "revision_11") ||
                  !strcmp(s_failure, "blank") || !strcmp(s_failure, "unknown") ||
                  !strcmp(s_failure, "adopt_on") || !strcmp(s_failure, "adopt_off") ||
                  !strcmp(s_failure, "fade_shared");
    CHECK(!mosaico_hardware_i2c_bus());
    int result = mosaico_hardware_init();
    if (rejected) {
        CHECK(result < 0 && s_efuse_reads == 1 && !s_gpio_calls && !s_register_reads &&
              !s_timer_calls && !s_i2c_calls && !mosaico_hardware_i2c_bus());
        puts("PASS mosaico_hardware revision guard before pin access");
        return 0;
    }
    if (!normal) {
        CHECK(result < 0 && s_failed && !mosaico_hardware_i2c_bus());
        if (strcmp(s_failure, "i2c")) {
            CHECK(!s_i2c_calls);
            if (s_timer_calls)
                CHECK(s_levels[60] == 1 && s_modes[60] == GPIO_MODE_OUTPUT);
        } else {
            CHECK(s_i2c_calls == 1 && s_levels[60] == 0);
        }
        unsigned timers_before = s_timer_calls;
        CHECK(mosaico_hardware_init() == 0);
        if (!strcmp(s_failure, "i2c"))
            CHECK(s_timer_calls ==
                  timers_before); // Do not power-cycle a completed rail on bus retry.
    } else {
        CHECK(result == 0);
        CHECK(s_degraded == (s_version != 0x0101 && s_version != 0x0102));
        CHECK(s_compatible == (s_version == 0x0101));
        if (!strcmp(s_failure, "adopt_on")) {
            CHECK(!s_timer_calls && !s_channel_calls && !s_install_calls && !s_wait_calls);
            CHECK(!strcmp(s_events[position("efuse") + 1], "vcc_on"));
        } else {
            CHECK(s_timer_calls == 1 && s_channel_calls == 1 && s_install_calls == 1 &&
                  s_fade_calls == 1 && s_wait_calls == 1 && s_stop_calls == 1);
            CHECK(position("efuse") < position("vcc_off") &&
                  position("vcc_off") < position("timer") &&
                  position("channel") < position("fade_255_to_0") &&
                  position("wait_done") < position("stop_on") &&
                  position("stop_on") < position("vcc_on") &&
                  position("vcc_on") < position("settled") &&
                  position("settled") < position("i2c"));
            CHECK(s_uninstall_calls == (s_shared_fade ? 0U : 1U));
        }
    }
    CHECK(mosaico_hardware_i2c_bus() == &s_bus && s_bus.open);
    CHECK(s_settle_calls == 1);
    unsigned events_before = s_event_count;
    CHECK(mosaico_hardware_init() == 0 && s_event_count == events_before);
    CHECK(s_fade_service == s_shared_fade);
    printf("PASS mosaico_hardware %s\n", argv[1]);
}
