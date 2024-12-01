#pragma once

#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

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

struct owl_toplevel;

float *
border_get_color(enum owl_border_state state);

void
toplevel_borders_create(struct owl_toplevel *toplevel);

void
toplevel_borders_set_size(struct owl_toplevel *toplevel,
                          uint32_t width, uint32_t height);

void
toplevel_borders_update(struct owl_toplevel *toplevel);

void
toplevel_borders_set_state(struct owl_toplevel *toplevel,
                           enum owl_border_state state);

struct wlr_scene_buffer *
surface_find_buffer(struct wlr_scene_node *node, struct wlr_surface *surface);

double
calculate_animation_curve_at(double x);

double
calculate_animation_passed(struct owl_animation *animation);

void
toplevel_initial_render(struct owl_toplevel *toplevel);

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel);
