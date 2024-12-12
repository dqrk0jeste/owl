#pragma once

#include <wayland-server-protocol.h>

#include "toplevel.h"
#include "output.h"

struct owl_workspace {
  struct wl_list link;
  struct owl_output *output;
  uint32_t index;
  struct wl_list masters;
  struct wl_list slaves;
  struct wl_list floating_toplevels;
  struct owl_toplevel *fullscreen_toplevel;
};

void
change_workspace(struct owl_workspace *workspace, bool keep_focus);

void
toplevel_move_to_workspace(struct owl_toplevel *toplevel,
                           struct owl_workspace *workspace);
