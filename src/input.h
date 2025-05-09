#pragma once

#include <wayland-server.h>

void
server_handle_new_input(struct wl_listener *listener, void *data);

void
server_handle_request_set_selection(struct wl_listener *listener, void *data);
