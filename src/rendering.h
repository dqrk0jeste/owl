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


double
find_animation_curve_at(double t);

struct owl_toplevel;

void
toplevel_draw_borders(struct owl_toplevel *toplevel);

void
toplevel_draw_placeholder(struct owl_toplevel *toplevel);

double
calculate_animation_passed(struct owl_animation *animation);

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel);

bool
toplevel_draw_frame(struct owl_toplevel *toplevel);

void
toplevel_apply_clip(struct owl_toplevel *toplevel);

void
toplevel_clip_to_size(struct owl_toplevel *toplevel,
                      uint32_t width, uint32_t height);

void
toplevel_unclip_size(struct owl_toplevel *toplevel);

struct owl_workspace;

void
workspace_draw_frame(struct owl_workspace *workspace);

void
scene_buffer_apply_opacity(struct wlr_scene_buffer *buffer,
                           int sx, int sy, void *user_data);

void
toplevel_handle_opacity(struct owl_toplevel *toplevel);

void
workspace_handle_opacity(struct owl_workspace *workspace);
