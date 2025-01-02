#pragma once

#include <stdbool.h>

#include <wayland-server.h>

void
server_handle_request_drag(struct wl_listener *listener, void *data);

void
server_handle_request_start_drag(struct wl_listener *listener, void *data);

void
server_handle_destroy_drag(struct wl_listener *listener, void *data);

void
dnd_icons_show(bool show);

void
dnd_icons_move(uint32_t x, uint32_t y);

