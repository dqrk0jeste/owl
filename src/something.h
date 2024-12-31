#pragma once

#include <wlr/types/wlr_compositor.h>

enum owl_type {
  OWL_TOPLEVEL,
  OWL_POPUP,
  OWL_LAYER_SURFACE,
};

struct owl_toplevel;
struct owl_layer_surface;

struct owl_something {
  enum owl_type type;
  union {
    struct owl_toplevel *toplevel;
    struct owl_popup *popup;
    struct owl_layer_surface *layer_surface;
  };
};

struct owl_something *
root_parent_of_surface(struct wlr_surface *wlr_surface);

struct owl_something *
something_at(double lx, double ly,
             struct wlr_surface **surface,
             double *sx, double *sy);
