#pragma once

#include <wayland-server.h>

void
cursor_shape_manager_handle_destroy(struct wl_listener *listener, void *data);

void
cursor_shape_manager_handle_request(struct wl_listener *listener, void *data);

void
cursor_handle_request(struct wl_listener *listener, void *data);

void
cursor_set_xcursor_variables(char *theme, uint32_t size);
