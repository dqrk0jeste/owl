#include "rendering.h"

#include "helpers.h"
#include "owl.h"
#include "config.h"
#include "toplevel.h"
#include "config.h"
#include "workspace.h"

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <wlr/util/log.h>

extern struct owl_server server;

void
toplevel_draw_borders(struct owl_toplevel *toplevel, uint32_t width, uint32_t height) {
  uint32_t border_width = server.config->border_width;

  float *border_color = toplevel->fullscreen
    ? (float[4]){ 0, 0, 0, 0 }
    : toplevel == server.focused_toplevel
      ? server.config->active_border_color
      : server.config->inactive_border_color;
  
  if(toplevel->borders[0] == NULL) {
    toplevel->borders[0] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 width + 2 * border_width,
                                                 border_width, border_color);
    wlr_scene_node_set_position(&toplevel->borders[0]->node, -border_width, -border_width);

    toplevel->borders[1] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 border_width, height, border_color);
    wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);

    toplevel->borders[2] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 width + 2 * border_width,
                                                 border_width, border_color);
    wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

    toplevel->borders[3] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 border_width, height, border_color);
    wlr_scene_node_set_position(&toplevel->borders[3]->node, -border_width, 0);
  } else {
    wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);
    wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

    wlr_scene_rect_set_size(toplevel->borders[0], width + 2 * border_width, border_width);
    wlr_scene_rect_set_size(toplevel->borders[1], border_width, height);
    wlr_scene_rect_set_size(toplevel->borders[2], width + 2 * border_width, border_width);
    wlr_scene_rect_set_size(toplevel->borders[3], border_width, height);
  }

  for(size_t i = 0; i < 4; i++) {
    wlr_scene_rect_set_color(toplevel->borders[i], border_color);
  }
}

void
toplevel_render_placeholder(struct owl_toplevel *toplevel, uint32_t width, uint32_t height) {
  if(toplevel->placeholder == NULL) {
    toplevel->placeholder = wlr_scene_rect_create(toplevel->scene_tree, width, height,
                                                  server.config->placeholder_color);
    wlr_scene_node_lower_to_bottom(&toplevel->placeholder->node);
  }
  wlr_scene_rect_set_size(toplevel->placeholder, width, height);
}

double
find_animation_curve_at(double t) {
  size_t down = 0;
  size_t up = BAKED_POINTS_COUNT - 1;

  size_t middle = (up + down) / 2;
  while(up - down != 1) {
    if(server.config->baked_points[middle].x <= t) {
      down = middle;  
    } else {
      up = middle;
    }
    middle = (up + down) / 2;
  }

  return server.config->baked_points[up].y;
}

double
calculate_animation_passed(struct owl_animation *animation) {
  return (double)animation->passed_frames / animation->total_frames;
}

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel) {
  double animation_passed = calculate_animation_passed(&toplevel->animation);
  double factor = find_animation_curve_at(animation_passed);

  uint32_t width = toplevel->animation.initial.width +
    (toplevel->current.width - toplevel->animation.initial.width) * factor;
  uint32_t height = toplevel->animation.initial.height +
    (toplevel->current.height - toplevel->animation.initial.height) * factor;

  toplevel_clip_to_size(toplevel, width, height); 
  toplevel_render_placeholder(toplevel, width, height);

  uint32_t x = toplevel->animation.initial.x +
    (toplevel->current.x - toplevel->animation.initial.x) * factor;
  uint32_t y = toplevel->animation.initial.y +
    (toplevel->current.y - toplevel->animation.initial.y) * factor;

  wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
  toplevel_draw_borders(toplevel, width, height);

  toplevel->animation.current = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  return animation_passed == 1.0;
}

bool
toplevel_draw_animation_frame(struct owl_toplevel *toplevel) {
  bool done = toplevel_animation_next_tick(toplevel);
  if(done) {
    toplevel->animation.running = false;
    if(toplevel->floating) {
      toplevel_unclip_size(toplevel);
    }
  } else {
    toplevel->animation.passed_frames++;
  }

  return done;
}

void
workspace_render_frame(struct owl_workspace *workspace) {
  bool animations_done = true;

  struct owl_toplevel *t;
  if(workspace->fullscreen_toplevel != NULL) {
    t = workspace->fullscreen_toplevel;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);

    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_unclip_size(t);
      wlr_scene_node_set_position(&t->scene_tree->node,
                                  t->current.x, t->current.y);
      toplevel_draw_borders(t, t->current.width, t->current.height);
      toplevel_render_placeholder(t, t->current.width, t->current.height);
    }
    return;
  }
  
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);

    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_unclip_size(t);
      wlr_scene_node_set_position(&t->scene_tree->node,
                                  t->current.x, t->current.y);
      toplevel_draw_borders(t, t->current.width, t->current.height);
      toplevel_render_placeholder(t, t->current.width, t->current.height);
    }
  }

  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);

    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_clip_to_fit(t);
      wlr_scene_node_set_position(&t->scene_tree->node,
                                  t->current.x, t->current.y);
      toplevel_draw_borders(t, t->current.width, t->current.height);
      toplevel_render_placeholder(t, t->current.width, t->current.height);
    }
  }

  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);

    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_clip_to_fit(t);
      wlr_scene_node_set_position(&t->scene_tree->node,
                                  t->current.x, t->current.y);
      toplevel_draw_borders(t, t->current.width, t->current.height);
      toplevel_render_placeholder(t, t->current.width, t->current.height);
    }
  }

  /* if there are animation that are not finished we request more frames
   * for the output, until all the animations are done */
  if(!animations_done) {
    wlr_output_schedule_frame(workspace->output->wlr_output);
  }
}

void
scene_buffer_apply_opacity(struct wlr_scene_buffer *buffer,
                           int sx, int sy, void *data) {
  struct window_rule_opacity *w = data;

  wlr_scene_buffer_set_opacity(buffer, w->value);
}

void
toplevel_handle_opacity(struct owl_toplevel *toplevel) {
  assert(toplevel->mapped);

  /*check for the opacity window rules */
  struct window_rule_opacity *w;
  wl_list_for_each(w, &server.config->window_rules.opacity, link) {
    /* cache this value in toplevel_handle_set_title() and toplevel_handle_set_app_id() */
    if(toplevel_matches_window_rule(toplevel, &w->condition)) {
      wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node, scene_buffer_apply_opacity, w);
      /* apply opacity to the placeholder rect so the surface is actually transperent */
      if(toplevel->placeholder != NULL) {
        float applied_opacity[4];
        applied_opacity[0] = server.config->placeholder_color[0];
        applied_opacity[1] = server.config->placeholder_color[1];
        applied_opacity[2] = server.config->placeholder_color[2];
        applied_opacity[3] = server.config->placeholder_color[3] * w->value;
        wlr_scene_rect_set_color(toplevel->placeholder, applied_opacity);
      }
    }
  }
}

void
workspace_handle_opacity(struct owl_workspace *workspace) {
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(!t->mapped) continue;
    toplevel_handle_opacity(t);
  }
  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped) continue;
    toplevel_handle_opacity(t);
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped) continue;
    toplevel_handle_opacity(t);
  }
}
