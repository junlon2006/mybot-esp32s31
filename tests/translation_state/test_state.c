/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_state_model.h"
#include <assert.h>
#include <stdio.h>

static const struct {
    mybot_device_state_t device;
    mybot_state_t public_state;
} mappings[] = {
    {MYBOT_DEVICE_STATE_UNPROVISIONED, MYBOT_STATE_PAIRING},
    {MYBOT_DEVICE_STATE_PAIRING, MYBOT_STATE_PAIRING},
    {MYBOT_DEVICE_STATE_AWAITING_CLAIM, MYBOT_STATE_PAIRING},
    {MYBOT_DEVICE_STATE_RUNTIME, MYBOT_STATE_READY},
    {MYBOT_DEVICE_STATE_IN_CONVERSATION, MYBOT_STATE_IN_CONVERSATION},
    {MYBOT_DEVICE_STATE_STARTING, MYBOT_STATE_TRANSLATION_STARTING},
    {MYBOT_DEVICE_STATE_STOPPING, MYBOT_STATE_TRANSLATION_STOPPING},
    {MYBOT_DEVICE_STATE_CLEANUP_PENDING, MYBOT_STATE_CLEANUP_PENDING},
    {MYBOT_DEVICE_STATE_START_UNKNOWN, MYBOT_STATE_START_UNKNOWN},
    {MYBOT_DEVICE_STATE_ERROR, MYBOT_STATE_TRANSLATION_ERROR},
};
static unsigned checks;

static void expect(const mybot_state_model_t *model, mybot_state_t public_state,
                   mybot_device_state_t device_state) {
    mybot_state_view_t actual = mybot_state_model_get_view(model);
    assert(actual.app_state == public_state);
    assert(actual.device_state == device_state);
    checks++;
}
static void ready(mybot_state_model_t *model) {
    mybot_state_model_reset(model);
    assert(mybot_state_model_begin_start(model));
    assert(mybot_state_model_begin_services(model));
    assert(mybot_state_model_services_ready(model));
}
static void test_all_device_states(void) {
    mybot_state_model_t model;
    ready(&model);
    for (unsigned i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
        assert(mybot_state_model_set_device_state(&model, mappings[i].device));
        expect(&model, mappings[i].public_state, mappings[i].device);
        assert(mybot_state_model_network_lost(&model));
        expect(&model, MYBOT_STATE_WIFI_DISCONNECTED, mappings[i].device);
        assert(mybot_state_model_network_restored(&model));
        expect(&model, mappings[i].public_state, mappings[i].device);
    }
    /* Ensure bit 3 is cleared when moving from states 8/9 back to lower states. */
    for (int i = 9; i >= 0; i--) {
        assert(mybot_state_model_set_device_state(&model, mappings[i].device));
        expect(&model, mappings[i].public_state, mappings[i].device);
    }
}
static void test_phase_priority(void) {
    for (unsigned i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
        mybot_state_model_t model;
        mybot_state_model_reset(&model);
        assert(mybot_state_model_set_device_state(&model, mappings[i].device));
        expect(&model, MYBOT_STATE_STOPPED, mappings[i].device);
        assert(!mybot_state_model_fail(&model));
        assert(mybot_state_model_begin_start(&model));
        expect(&model, MYBOT_STATE_WIFI_PROVISIONING, mappings[i].device);
        assert(!mybot_state_model_network_restored(&model));
        assert(mybot_state_model_begin_services(&model));
        expect(&model, MYBOT_STATE_STARTING_SERVICES, mappings[i].device);
        assert(mybot_state_model_services_ready(&model));
        assert(mybot_state_model_fail(&model));
        expect(&model, MYBOT_STATE_FAILED, mappings[i].device);
        assert(!mybot_state_model_network_restored(&model));
        assert(!mybot_state_model_fail(&model));
        mybot_state_model_begin_stop(&model);
        expect(&model, MYBOT_STATE_STOPPING, mappings[i].device);
        assert(!mybot_state_model_fail(&model));
        assert(!mybot_state_model_network_restored(&model));
        assert(!mybot_state_model_begin_start(&model));
        mybot_state_model_reset(&model);
        expect(&model, MYBOT_STATE_STOPPED, MYBOT_DEVICE_STATE_UNPROVISIONED);
    }
}
static void test_invalid_and_repeated_reset(void) {
    mybot_state_model_t model;
    ready(&model);
    assert(mybot_state_model_set_device_state(&model, MYBOT_DEVICE_STATE_START_UNKNOWN));
    assert(!mybot_state_model_set_device_state(&model, (mybot_device_state_t)-1));
    assert(!mybot_state_model_set_device_state(&model, (mybot_device_state_t)10));
    expect(&model, MYBOT_STATE_START_UNKNOWN, MYBOT_DEVICE_STATE_START_UNKNOWN);
    for (unsigned i = 0; i < 20; i++) {
        mybot_state_model_begin_stop(&model);
        mybot_state_model_reset(&model);
        mybot_state_model_reset(&model);
        expect(&model, MYBOT_STATE_STOPPED, MYBOT_DEVICE_STATE_UNPROVISIONED);
        ready(&model);
        assert(mybot_state_model_set_device_state(&model, MYBOT_DEVICE_STATE_ERROR));
        expect(&model, MYBOT_STATE_TRANSLATION_ERROR, MYBOT_DEVICE_STATE_ERROR);
    }
}
int main(void) {
    test_all_device_states();
    test_phase_priority();
    test_invalid_and_repeated_reset();
    printf("translation state: %u public projection assertions passed\n", checks);
    return 0;
}
