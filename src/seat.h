#pragma once

#include <stdbool.h>
#include <wayland-server.h>

struct seat {
    struct wlr_seat *base;

    struct wl_listener new_input;
    struct wl_listener request_set_selection;
    struct wl_listener request_drag;
    struct wl_listener request_start_drag;
    struct wl_listener request_destroy_drag;
};

void
dnd_icons_move(int x, int y);

void
seat_init(void);
