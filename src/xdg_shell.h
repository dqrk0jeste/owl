#pragma once

#include <wayland-server-core.h>

struct xdg_shell {
    struct wlr_xdg_shell *base;
    struct wl_listener new_toplevel;
    struct wl_listener new_popup;

    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wl_listener new_decoration;

    struct wlr_server_decoration_manager *kde_decoration_manager;

    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener request_activation;
};

void
xdg_shell_init(void);
