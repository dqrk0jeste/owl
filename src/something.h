#pragma once
#include "toplevel.h"
#include "popup.h"
#include "layer_surface.h"

enum owl_type {
  OWL_TOPLEVEL,
  OWL_POPUP,
  OWL_LAYER_SURFACE,
};

struct owl_something {
  enum owl_type type;
  union {
    struct owl_toplevel *toplevel;
    struct owl_popup *popup;
    struct owl_layer_surface *layer_surface;
  };
};

