#pragma once

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>

struct foreign_toplevel_handle {
    struct wlr_foreign_toplevel_handle_v1 *wlr_handle;
    struct toplevel *toplevel;

    struct wl_listener request_activate;
    struct wl_listener request_fullscreen;
    struct wl_listener request_close;
    struct wl_listener set_rectangle;
};

struct foreign_toplevel_handle *
foreign_toplevel_handle_create(struct toplevel *toplevel);

void
foreign_toplevel_handle_destroy(struct foreign_toplevel_handle *handle);
