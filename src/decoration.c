#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "decoration.h"

void
server_handle_request_xdg_decoration(struct wl_listener *listener, void *data) {
  struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
  wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                                          WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

