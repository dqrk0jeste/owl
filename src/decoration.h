#pragma once

#include <wayland-server-core.h>

void
server_handle_request_xdg_decoration(struct wl_listener *listener, void *data);
