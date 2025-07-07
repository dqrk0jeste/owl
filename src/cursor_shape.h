#pragma once

#include <wayland-server-core.h>

struct cursor_shape_manager {
    struct wlr_cursor_shape_manager_v1 *base;

    struct wl_listener set_cursor_shape;
    struct wl_listener destroy;
};

void
cursor_shape_manager_init(void);
