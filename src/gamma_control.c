#include "gamma_control.h"

#include <wlr/types/wlr_output.h>

#include "mwc.h"

extern struct server server;

static void
handle_set_gamma(struct wl_listener *listener, void *data) {
    struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    struct wlr_gamma_control_v1 *gamma_control =
            wlr_gamma_control_manager_v1_get_control(server.gamma_manager.base, event->output);

    if(!wlr_gamma_control_v1_apply(gamma_control, &state)) {
        wlr_output_state_finish(&state);
        return;
    }

    if(!wlr_output_commit_state(event->output, &state)) {
        wlr_gamma_control_v1_send_failed_and_destroy(gamma_control);
    }

    wlr_output_state_finish(&state);
}

void
gamma_manager_init(void) {
    server.gamma_manager.base = wlr_gamma_control_manager_v1_create(server.display);
    server.gamma_manager.set_gamma.notify = handle_set_gamma;
    wl_signal_add(&server.gamma_manager.base->events.set_gamma, &server.gamma_manager.set_gamma);
}
