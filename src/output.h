#pragma once

#include <wlr/types/wlr_output.h>

#include "workspace.h"

struct owl_output {
	struct wl_list link;
	struct wlr_output *wlr_output;
  struct wl_list workspaces;
  struct wlr_box usable_area;
  struct {
    struct wl_list background;
    struct wl_list bottom;
    struct wl_list top;
    struct wl_list overlay;
  } layers;
  struct owl_workspace *active_workspace;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

