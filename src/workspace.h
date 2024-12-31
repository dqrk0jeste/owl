#pragma once

#include "config.h"
#include "toplevel.h"
#include "output.h"

#include <wayland-server-protocol.h>

struct owl_animation;

struct owl_workspace {
  struct wl_list link;

  struct owl_output *output;
  uint32_t index;
  struct workspace_config *config;

  struct wl_list masters;
  struct wl_list slaves;
  struct wl_list floating_toplevels;
  struct owl_toplevel *fullscreen_toplevel;
};

void
workspace_create_for_output(struct owl_output *output, struct workspace_config *config);

void
change_workspace(struct owl_workspace *workspace, bool keep_focus);

void
toplevel_move_to_workspace(struct owl_toplevel *toplevel, struct owl_workspace *workspace);
