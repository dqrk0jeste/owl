#include "gamma_control.h"

#include "owl.h"

#include <wlr/types/wlr_output.h>

extern struct owl_server server;

void
gamma_control_set_gamma(struct wl_listener *listener, void *data) {
  struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;

  struct wlr_output_state state;
	wlr_output_state_init(&state);

	struct wlr_gamma_control_v1 *gamma_control =
    wlr_gamma_control_manager_v1_get_control(server.gamma_control_manager, event->output);

	if(!wlr_gamma_control_v1_apply(gamma_control, &state)) {
		wlr_output_state_finish(&state);
		return;
	}

	if(!wlr_output_commit_state(event->output, &state)) {
		wlr_gamma_control_v1_send_failed_and_destroy(gamma_control);
	}

	wlr_output_state_finish(&state);
}
