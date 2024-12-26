#pragma once

#include <stdint.h>
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
  struct wlr_box initial;
  struct wlr_box current;
};

struct owl_toplevel;

void
toplevel_draw_borders(struct owl_toplevel *toplevel, uint32_t width, uint32_t height);


struct wlr_scene_buffer *
scene_node_find_buffer(struct wlr_scene_node *node, struct wlr_surface *surface);

double
find_animation_curve_at(double t);

double
calculate_animation_passed(struct owl_animation *animation);

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel);

bool toplevel_draw_animation_frame(struct owl_toplevel *toplevel);

struct owl_workspace;

void workspace_render_frame(struct owl_workspace *workspace);

void toplevel_handle_opacity(struct owl_toplevel *toplevel);

void workspace_handle_opacity(struct owl_workspace *workspace);
