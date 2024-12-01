#include "rendering.h"

#include "owl.h"
#include "config.h"
#include "something.h"
#include "toplevel.h"

#include <stdlib.h>
#include <assert.h>

extern struct owl_server server;

/* TODO: replace with a lookup table */
float *
border_get_color(enum owl_border_state state) {
  static float invisible[] = {0, 0, 0, 0};
  switch(state) {
    case OWL_BORDER_INVISIBLE:
      return invisible;
    case OWL_BORDER_ACTIVE:
      return server.config->active_border_color;
    case OWL_BORDER_INACTIVE:
      return server.config->inactive_border_color;
  }
}

void
toplevel_borders_create(struct owl_toplevel *toplevel) {
  uint32_t width, height;
  if(toplevel->floating) {
    width = WIDTH(toplevel);
    height = HEIGHT(toplevel);
  } else {
    width = toplevel->pending.width;
    height = toplevel->pending.height;
  }

  uint32_t border_width = server.config->border_width;
  const float *border_color = border_get_color(OWL_BORDER_INVISIBLE);

  toplevel->borders[0] = wlr_scene_rect_create(toplevel->scene_tree,
                                               width + 2 * border_width,
                                               border_width, border_color);
  wlr_scene_node_set_position(&toplevel->borders[0]->node,
                              -border_width, -border_width);

  toplevel->borders[1] = wlr_scene_rect_create(toplevel->scene_tree,
                                               border_width, height, border_color);
  wlr_scene_node_set_position(&toplevel->borders[1]->node,
                              width, 0);

  toplevel->borders[2] = wlr_scene_rect_create(toplevel->scene_tree,
                                               width + 2 * border_width,
                                               border_width, border_color);
  wlr_scene_node_set_position(&toplevel->borders[2]->node,
                              -border_width, height);

  toplevel->borders[3] = wlr_scene_rect_create(toplevel->scene_tree,
                                               border_width, height, border_color);
  wlr_scene_node_set_position(&toplevel->borders[3]->node,
                              -border_width, 0);
}

void
toplevel_borders_set_size(struct owl_toplevel *toplevel,
                          uint32_t width, uint32_t height) {
  uint32_t border_width = server.config->border_width;

  wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);
  wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

  wlr_scene_rect_set_size(toplevel->borders[0], width + 2 * border_width, border_width);
  wlr_scene_rect_set_size(toplevel->borders[1], border_width, height);
  wlr_scene_rect_set_size(toplevel->borders[2], width + 2 * border_width, border_width);
  wlr_scene_rect_set_size(toplevel->borders[3], border_width, height);
}

void
toplevel_borders_update(struct owl_toplevel *toplevel) {
  uint32_t width, height;
  if(toplevel->floating) {
    width = WIDTH(toplevel);
    height = HEIGHT(toplevel);
  } else {
    width = toplevel->pending.width;
    height = toplevel->pending.height;
  }

  uint32_t border_width = server.config->border_width;

  wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);
  wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

  wlr_scene_rect_set_size(toplevel->borders[0], width + 2 * border_width, border_width);
  wlr_scene_rect_set_size(toplevel->borders[1], border_width, height);
  wlr_scene_rect_set_size(toplevel->borders[2], width + 2 * border_width, border_width);
  wlr_scene_rect_set_size(toplevel->borders[3], border_width, height);
}

void
toplevel_borders_set_state(struct owl_toplevel *toplevel,
                           enum owl_border_state state) {
  const float *border_color = border_get_color(state);
  for(size_t i = 0; i < 4; i++) {
    wlr_scene_rect_set_color(toplevel->borders[i], border_color);
  }
}

struct wlr_scene_buffer *
surface_find_buffer(struct wlr_scene_node *node, struct wlr_surface *surface) {
  if(node->type == WLR_SCENE_NODE_BUFFER) {
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

    struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(scene_buffer);
    if(!scene_surface) {
      return NULL;
    }

    struct wlr_surface *s = scene_surface->surface;

    if(s && s == surface) {
      return scene_buffer;
    }
  }

  if(node->type == WLR_SCENE_NODE_TREE) {
    struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);

    struct wlr_scene_node *child;
    wl_list_for_each(child, &scene_tree->children, link) {
      struct wlr_scene_buffer *found_buffer = surface_find_buffer(child, surface);
      if(found_buffer) {
        return found_buffer;
      }
    }
  }

  return NULL;
}

double
calculate_animation_curve_at(double x) {
  double a = server.config->animation_curve[0];
  double b = server.config->animation_curve[1];
  double c = server.config->animation_curve[2];

  return (a * x * x * x + b * x * x + c * x) / (a + b + c);
}

double
calculate_animation_passed(struct owl_animation *animation) {
  double passed = (double)animation->passed_frames / animation->total_frames;

  return calculate_animation_curve_at(passed);
}

void
toplevel_initial_render(struct owl_toplevel *toplevel) {
  assert(toplevel->scene_tree == NULL);

  /* add this toplevel to the scene tree */
  if(toplevel->floating) {
    toplevel->scene_tree = wlr_scene_xdg_surface_create(server.floating_tree,
                                                        toplevel->xdg_toplevel->base);
  } else {
    toplevel->scene_tree = wlr_scene_xdg_surface_create(server.tiled_tree,
                                                        toplevel->xdg_toplevel->base);
  }

  toplevel_borders_create(toplevel);
  focus_toplevel(toplevel);

  /* we are keeping toplevels scene_tree in this free user data field, it is used in 
   * assigning parents to popups */
  toplevel->xdg_toplevel->base->data = toplevel->scene_tree;

  /* in the node we want to keep information what that node represents. we do that
   * be keeping owl_something in user data field, which is a union of all possible
   * 'things' we can have on the screen */
  struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_TOPLEVEL;
  something->toplevel = toplevel;

  toplevel->scene_tree->node.data = something;
}

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel) {
  double animation_passed = calculate_animation_passed(&toplevel->animation);

  uint32_t width = toplevel->animation.initial_geometry.width +
    (toplevel->pending.width - toplevel->animation.initial_geometry.width) * animation_passed;
  uint32_t height = toplevel->animation.initial_geometry.height +
    (toplevel->pending.height - toplevel->animation.initial_geometry.height) * animation_passed;

  /*if(width > WIDTH(toplevel) || height > HEIGHT(toplevel)) {*/
  /*  struct wlr_scene_buffer *scene_buffer = surface_find_buffer(&toplevel->scene_tree->node,*/
  /*                                                              toplevel->xdg_toplevel->base->surface);*/
  /*  wlr_scene_buffer_set_dest_size(scene_buffer, width, height);*/
  /*} else {*/
  /*  toplevel_clip_to_size(toplevel, width, height); */
  /*}*/

  if(!toplevel->rendered) {
    toplevel_initial_render(toplevel);
    toplevel->rendered = true;
  }

  /*toplevel_borders_set_size(toplevel, width, height);*/

  uint32_t x = toplevel->animation.initial_geometry.x +
    (toplevel->pending.x - toplevel->animation.initial_geometry.x) * animation_passed;
  uint32_t y = toplevel->animation.initial_geometry.y +
    (toplevel->pending.y - toplevel->animation.initial_geometry.y) * animation_passed;

  /*if(toplevel->animation.passed_frames == 0) {*/
  /*  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);*/
  /*}*/

  wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);

  toplevel->animation.current_geometry = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  return animation_passed == 1.0;
}

