#pragma once

#include <wlr/util/box.h>

enum owl_border_state {
  OWL_BORDER_INVISIBLE,
  OWL_BORDER_ACTIVE,
  OWL_BORDER_INACTIVE,
};

struct owl_animation {
  bool should_animate;
  bool running;
  uint32_t total_frames;
  uint32_t passed_frames;
  struct wlr_box initial_geometry;
  struct wlr_box current_geometry;
};

