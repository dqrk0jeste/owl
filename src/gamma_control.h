#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_gamma_control_v1.h>

struct gamma_manager {
    struct wlr_gamma_control_manager_v1 *base;

    struct wl_listener set_gamma;
};

void
gamma_manager_init(void);
