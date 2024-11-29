#include "owl.h"
#include "ipc.h"

#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr/util/log.h"
#include "xdg-shell-protocol.h"
#include <stdint.h>
#include <sys/types.h>
#include <wayland-util.h>

/* we initialize an instance of our global state */
struct owl_server server;

/* handles child processes */
static void
sigchld_handler(int signo) {
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

/* these next few functions are some helpers */
static void
run_cmd(char *cmd) {
  if(fork() == 0) {
    execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
  }
}

static int
box_area(struct wlr_box *box) {
  return box->width * box->height;
}

static double
frame_duration(uint32_t refresh_rate_mhz) {
  return 1000000.0 / refresh_rate_mhz;
}

static double
calculate_animation_curve_at(double x) {
  double a = server.config->animation_curve[0];
  double b = server.config->animation_curve[1];
  double c = server.config->animation_curve[2];

  return (a * x * x * x + b * x * x + c * x) / (a + b + c);
}

static double
calculate_animation_passed(struct owl_animation *animation) {
  double passed = (double)animation->passed_frames / animation->total_frames;

  return calculate_animation_curve_at(passed);
}

static void
calculate_masters_dimensions(struct owl_output *output, uint32_t master_count,
                             uint32_t slave_count, uint32_t *width, uint32_t *height) {
  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;
  double border_width = server.config->border_width;

  struct wlr_box output_box = output->usable_area;

  uint32_t total_width = slave_count > 0
    ? output_box.width * master_ratio
    : output_box.width;

  uint32_t total_decorations = slave_count > 0
    ? outer_gaps // left outer gaps
    + master_count * 2 * border_width // all borders
    + (master_count - 1) * 2 * inner_gaps // inner gaps between masters
    + inner_gaps // right inner gaps 
    : outer_gaps // left outer gaps
    + master_count * 2 * border_width // all borders
    + (master_count - 1) * 2 * inner_gaps // inner gaps between masters
    + outer_gaps; // right outer gaps

  *width = (total_width - total_decorations) / master_count;
  *height = output_box.height - 2 * outer_gaps - 2 * border_width;
}

static void
calculate_slaves_dimensions(struct owl_output *output, uint32_t slave_count,
                            uint32_t *width, uint32_t *height) {
  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;
  double border_width = server.config->border_width;

  struct wlr_box output_box = output->usable_area;

  *width = output_box.width * (1 - master_ratio)
    - outer_gaps - inner_gaps
    - 2 * border_width;
  *height = (output_box.height - 2 * outer_gaps
    - (slave_count - 1) * 2 * inner_gaps
    - slave_count * 2 * border_width) / slave_count;
}

static bool
toplevel_is_master(struct owl_toplevel *toplevel) {
  struct owl_toplevel *t;
  wl_list_for_each(t, &toplevel->workspace->masters, link) {
    if(toplevel == t) return true;
  };
  return false;
}

static bool
toplevel_is_slave(struct owl_toplevel *toplevel) {
  struct owl_toplevel *t;
  wl_list_for_each(t, &toplevel->workspace->slaves, link) {
    if(toplevel == t) return true;
  };
  return false;
}

static bool
toplevel_position_changed(struct owl_toplevel *toplevel) {
  return X(toplevel) != toplevel->pending.x
    || Y(toplevel) != toplevel->pending.y;
}
static bool
toplevel_size_changed(struct owl_toplevel *toplevel) {
  return WIDTH(toplevel) != toplevel->pending.width
    || HEIGHT(toplevel) != toplevel->pending.height;
}

static bool
toplevel_matches_window_rule(struct owl_toplevel *toplevel,
                             struct window_rule_regex *condition) {
  char *app_id = toplevel->xdg_toplevel->app_id;
  char *title = toplevel->xdg_toplevel->title;

  bool matches_app_id;
  if(condition->has_app_id_regex) {
    if(app_id == NULL) {
      matches_app_id = false;
    } else {
      matches_app_id = regexec(&condition->app_id_regex, app_id, 0, NULL, 0) == 0;
    }
  } else {
    matches_app_id = true;
  }

  bool matches_title;
  if(condition->has_title_regex) {
    if(title == NULL) {
      matches_title = false;
    } else {
      matches_title = regexec(&condition->title_regex, title, 0, NULL, 0) == 0;
    }
  } else {
    matches_title = true;
  }

  if(matches_app_id && matches_title) {
    return true;
  }

  return false;
}

static void
toplevel_floating_size(struct owl_toplevel *toplevel, uint32_t *width, uint32_t *height) {
  char *app_id = toplevel->xdg_toplevel->app_id;
  char *title = toplevel->xdg_toplevel->title;

  struct window_rule_size *w;
  wl_list_for_each(w, &server.config->window_rules.size, link) {
    if(toplevel_matches_window_rule(toplevel, &w->condition)) {
      if(w->relative_width) {
        *width = toplevel->workspace->output->usable_area.width * w->width / 100;
      } else {
        *width = w->width;
      }

      if(w->relative_height) {
        *height = toplevel->workspace->output->usable_area.height * w->height / 100;
      } else {
        *height = w->height;
      }

      return;
    }
  }

  *width = WIDTH(toplevel);
  *height = HEIGHT(toplevel);
}

static bool
toplevel_should_float(struct owl_toplevel *toplevel) {
  /* we make toplevels float if they have fixed size
   * or are children of another toplevel */
  bool b =
    (toplevel->xdg_toplevel->current.max_height &&
    toplevel->xdg_toplevel->current.max_height
    == toplevel->xdg_toplevel->current.min_height)
    || (toplevel->xdg_toplevel->current.max_width &&
    toplevel->xdg_toplevel->current.max_width
    == toplevel->xdg_toplevel->current.min_width)
    || toplevel->xdg_toplevel->parent != NULL;
  if(b) return true;

  char *app_id = toplevel->xdg_toplevel->app_id;
  char *title = toplevel->xdg_toplevel->title;

  struct window_rule_float *w;
  wl_list_for_each(w, &server.config->window_rules.floating, link) {
    if(toplevel_matches_window_rule(toplevel, &w->condition)) {
      return true;
    }
  }

  return false;
}

struct owl_output *
output_get_relative(struct owl_output *output, enum owl_direction direction) {
  struct wlr_box original_output_box;
  wlr_output_layout_get_box(server.output_layout,
                            output->wlr_output, &original_output_box);

  uint32_t original_output_midpoint_x =
    original_output_box.x + original_output_box.width / 2;
  uint32_t original_output_midpoint_y =
    original_output_box.y + original_output_box.height / 2;

  struct owl_output *o;
  wl_list_for_each(o, &server.outputs, link) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);

    if(direction == OWL_LEFT &&
      original_output_box.x == output_box.x + output_box.width
      && original_output_midpoint_y > output_box.y
      && original_output_midpoint_y < output_box.y + output_box.height) {
      return o;
    } else if(direction == OWL_RIGHT
      && original_output_box.x + original_output_box.width == output_box.x
      && original_output_midpoint_y > output_box.y
      && original_output_midpoint_y < output_box.y + output_box.height) {
      return o;
    } else if(direction == OWL_UP
      && original_output_box.y == output_box.y + output_box.height
      && original_output_midpoint_x > output_box.x
      && original_output_midpoint_x < output_box.x + output_box.width) {
      return o;
    } else if(direction == OWL_DOWN
      && original_output_box.y + original_output_box.height == output_box.y
      && original_output_midpoint_x > output_box.x
      && original_output_midpoint_x < output_box.x + output_box.width) {
      return o;
    }
  }

  return NULL;
}

static struct owl_something *
root_parent_of_surface(struct wlr_surface *wlr_surface) {
  struct wlr_surface *root_wlr_surface =
    wlr_surface_get_root_surface(wlr_surface);
  struct wlr_xdg_surface *xdg_surface =
    wlr_xdg_surface_try_from_wlr_surface(root_wlr_surface);

  struct wlr_scene_tree *tree;
  if(xdg_surface != NULL) {
    tree = xdg_surface->data;
  } else {
    struct wlr_layer_surface_v1 *layer_surface =
      wlr_layer_surface_v1_try_from_wlr_surface(root_wlr_surface);
    if(layer_surface == NULL) {
      return NULL;
    }
    tree = xdg_surface->data;
  }

  struct owl_something *something = tree->node.data;
  while(something == NULL || something->type == OWL_POPUP) {
    tree = tree->node.parent;
    something = tree->node.data;
  }

  return something;
}

/* TODO: return owl_something and check for layer_surfaces */
static struct owl_toplevel *
get_pointer_focused_toplevel(void) {
  struct wlr_surface *focused_surface = server.seat->pointer_state.focused_surface;
  if(focused_surface == NULL) {
    return NULL;
  }

  struct owl_something *something = root_parent_of_surface(focused_surface);
  if(something->type == OWL_TOPLEVEL) {
    return something->toplevel;
  }

  return NULL;
}

static void
cursor_jump_focused_toplevel(void) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  struct wlr_box geo_box = toplevel->xdg_toplevel->base->geometry;
  wlr_cursor_warp(server.cursor, NULL,
                  toplevel->scene_tree->node.x + geo_box.x + geo_box.width / 2.0,
                  toplevel->scene_tree->node.y + geo_box.y + geo_box.height / 2.0);
}

/* TODO: replace with a lookup table */
static float *
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

static void
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

static void
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

static void
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

static void
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

static void
toplevel_center_floating(struct owl_toplevel *toplevel) {
  assert(toplevel->floating);

  struct wlr_box output_box = toplevel->workspace->output->usable_area;
  toplevel->pending.x = output_box.x + (output_box.width - WIDTH(toplevel)) / 2;
  toplevel->pending.y = output_box.y + (output_box.height - HEIGHT(toplevel)) / 2;
}

static void
toplevel_set_initial_state(struct owl_toplevel *toplevel, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
  assert(!toplevel->mapped);

  if(server.config->animations) {
    toplevel->animation.should_animate = true;
    toplevel->animation.initial_geometry = (struct wlr_box){
      .x = x + width / 2,
      .y = y + height / 2,
      .width = 0,
      .height = 0,
    };
  } else {
    toplevel->animation.should_animate = false;
  }

  toplevel->animation.initial_geometry = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  toplevel->configure_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                                                         width, height);
  toplevel->dirty = true;
}

static void
toplevel_commit(struct owl_toplevel *toplevel);

static void
toplevel_set_pending_state(struct owl_toplevel *toplevel, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
  assert(toplevel->mapped);

  struct wlr_box current = (struct wlr_box){
    .x = X(toplevel),
    .y = Y(toplevel),
    .width = WIDTH(toplevel),
    .height = HEIGHT(toplevel),
  };

  struct wlr_box pending = {
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  toplevel->pending = pending;

  if(!server.config->animations || toplevel == server.grabbed_toplevel
     || wlr_box_equal(&current, &pending)) {
    toplevel->animation.should_animate = false;
  } else {
    toplevel->animation.should_animate = true;
    toplevel->animation.initial_geometry = current;
  }

  if((toplevel->floating || toplevel->fullscreen)
     && !toplevel_size_changed(toplevel)) {
    toplevel_commit(toplevel);
    return;
  };

  toplevel->configure_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                                                         width, height);
  toplevel->dirty = true;
}

static void
cursor_jump_output(struct owl_output *output) {
  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  wlr_cursor_warp(server.cursor, NULL,
                  output_box.x + output_box.width / 2.0,
                  output_box.y + output_box.height / 2.0);
}

static void
unfocus_focused_toplevel() {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  server.focused_toplevel = NULL;

  if(!toplevel->fullscreen) {
    toplevel_borders_set_state(toplevel, OWL_BORDER_INACTIVE);
  }
  /* deactivate the surface */
  wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
  /* clear all focus on the keyboard, focusing new should set new toplevel focus */
  wlr_seat_keyboard_clear_focus(server.seat);

  ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
}

static void
focus_toplevel(struct owl_toplevel *toplevel) {
  assert(toplevel != NULL);

  if(server.layer_exclusive_keyboard != NULL) return;

  if(toplevel->workspace->fullscreen_toplevel != NULL
    && toplevel != toplevel->workspace->fullscreen_toplevel) return;

  struct owl_toplevel *prev_toplevel = server.focused_toplevel;
  if(prev_toplevel == toplevel) return;

  if(prev_toplevel != NULL) {
    wlr_xdg_toplevel_set_activated(prev_toplevel->xdg_toplevel, false);
    if(!toplevel->fullscreen) {
      toplevel_borders_set_state(prev_toplevel, OWL_BORDER_INACTIVE);
    }
  }

  server.focused_toplevel = toplevel;

  if(toplevel->floating) {
    wl_list_remove(&toplevel->link);
    wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
  }

  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

  if(!toplevel->fullscreen) {
    toplevel_borders_set_state(toplevel, OWL_BORDER_ACTIVE);
  }

  struct wlr_seat *seat = server.seat;
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  if(keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(seat, toplevel->xdg_toplevel->base->surface,
                                   keyboard->keycodes, keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }

  ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
}

static struct owl_toplevel *
workspace_find_closest_tiled_toplevel(struct owl_workspace *workspace, bool master,
                                      enum owl_direction side) {
  /* this means there are no tiled toplevels */
  if(wl_list_empty(&workspace->masters)) return NULL;

  struct owl_toplevel *first_master = wl_container_of(workspace->masters.next,
                                                      first_master, link);
  struct owl_toplevel *last_master = wl_container_of(workspace->masters.prev,
                                                     last_master, link);

  struct owl_toplevel *first_slave = NULL;
  struct owl_toplevel *last_slave = NULL;
  if(!wl_list_empty(&workspace->slaves)) {
    first_slave = wl_container_of(workspace->slaves.next, first_slave, link);
    last_slave = wl_container_of(workspace->slaves.prev, last_slave, link);
  }

  switch(side) {
    case OWL_UP: {
      if(master || first_slave == NULL) return first_master;
      return first_slave;
    }
    case OWL_DOWN: {
      if(master || last_slave == NULL) return first_master;
      return last_slave;
    }
    case OWL_LEFT: {
      return first_master;
    }
    case OWL_RIGHT: {
      if(first_slave != NULL) return first_slave;
      return last_master;
    }
  }
}

static struct owl_toplevel *
workspace_find_closest_floating_toplevel(struct owl_workspace *workspace,
                                         enum owl_direction side) {
  struct wl_list *l = workspace->floating_toplevels.next;
  if(l == &workspace->floating_toplevels) return NULL;

  struct owl_toplevel *t = wl_container_of(l, t, link);

  struct owl_toplevel *min_x = t;
  struct owl_toplevel *max_x = t;
  struct owl_toplevel *min_y = t;
  struct owl_toplevel *max_y = t;

  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(X(t) < X(min_x)) {
      min_x = t;
    } else if(X(t) > X(max_x)) {
      max_x = t;
    }
    if(Y(t) < Y(min_y)) {
      min_y = t;
    } else if(Y(t) > Y(max_y)) {
      max_y = t;
    }
  }

  switch(side) {
    case OWL_UP: return min_y;
    case OWL_DOWN: return max_y;
    case OWL_LEFT: return min_x;
    case OWL_RIGHT: return max_x;
  }
}

static struct owl_toplevel *
toplevel_find_closest_floating_on_workspace(struct owl_toplevel *toplevel,
                                            enum owl_direction direction) {
  assert(toplevel->floating);
  struct owl_workspace *workspace = toplevel->workspace;

  struct owl_toplevel *min = NULL;
  uint32_t min_val = UINT32_MAX;

  struct owl_toplevel *t;
  switch(direction) {
    case OWL_UP: {
      wl_list_for_each(t, &workspace->floating_toplevels, link) {
        if(t == toplevel || Y(t) > Y(toplevel)) continue;

        uint32_t dy = abs((int)Y(toplevel) - Y(t));
        if(dy < min_val) {
          min = t;
          min_val = dy;
        }
      }
      return min;
    }
    case OWL_DOWN: {
      wl_list_for_each(t, &workspace->floating_toplevels, link) {
        if(t == toplevel || Y(t) < Y(toplevel)) continue;

        uint32_t dy = abs((int)Y(toplevel) - Y(t));
        if(dy < min_val) {
          min = t;
          min_val = dy;
        }
      }
      return min;
    }
    case OWL_LEFT: {
      wl_list_for_each(t, &workspace->floating_toplevels, link) {
        if(t == toplevel || X(t) > X(toplevel)) continue;

        uint32_t dx = abs((int)X(toplevel) - X(t));
        if(dx < min_val) {
          min = t;
          min_val = dx;
        }
      }
      return min;
    }
    case OWL_RIGHT: {
      wl_list_for_each(t, &workspace->floating_toplevels, link) {
        if(t == toplevel || X(t) < X(toplevel)) continue;

        uint32_t dx = abs((int)X(toplevel) - X(t));
        if(dx < min_val) {
          min = t;
          min_val = dx;
        }
      }
      return min;
    }
  }
}

static void
focus_output(struct owl_output *output,
             enum owl_direction side) {
  assert(output != NULL);

  struct owl_toplevel *focus_next = NULL;
  struct owl_workspace *workspace = output->active_workspace;

  if(workspace->fullscreen_toplevel != NULL) {
    focus_next = workspace->fullscreen_toplevel;
  } else if(server.focused_toplevel == NULL || !server.focused_toplevel->floating) {
    bool master = server.focused_toplevel != NULL
      ? toplevel_is_master(server.focused_toplevel)
      : true;
    focus_next = workspace_find_closest_tiled_toplevel(output->active_workspace,
                                                       master, side);
    /* if there are no tiled toplevels we try floating */
    if(focus_next == NULL) {
      focus_next = workspace_find_closest_floating_toplevel(output->active_workspace,
                                                            side);
    }
  } else {
    focus_next = workspace_find_closest_floating_toplevel(output->active_workspace,
                                                          side);
    /* if there are no floating toplevels we try tiled */
    if(focus_next == NULL) {
      focus_next = workspace_find_closest_tiled_toplevel(output->active_workspace,
                                                         true, side);
    }
  }

  server.active_workspace = workspace;
  ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

  if(focus_next == NULL) {
    unfocus_focused_toplevel();
    cursor_jump_output(output);
  } else {
    focus_toplevel(focus_next);
    cursor_jump_focused_toplevel();
  }
}

static void
focus_layer_surface(struct owl_layer_surface *layer_surface) {
  enum zwlr_layer_surface_v1_keyboard_interactivity keyboard_interactive =
    layer_surface->wlr_layer_surface->current.keyboard_interactive;

  switch(keyboard_interactive) {
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE: {
      return;
    }
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE: {
      server.prev_focused = server.focused_toplevel;
      unfocus_focused_toplevel();
      server.layer_exclusive_keyboard = layer_surface;
      struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
      if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat, layer_surface->wlr_layer_surface->surface,
                                       keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
      }
      return;
    }
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND: {
      if(server.layer_exclusive_keyboard != NULL) return;
      server.prev_focused = server.focused_toplevel;
      unfocus_focused_toplevel();
      struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
      if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat,
                                       layer_surface->wlr_layer_surface->surface,
                                       keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
      }
      return;
    }
  }
}

static struct owl_output *
toplevel_get_primary_output(struct owl_toplevel *toplevel) {
  uint32_t toplevel_x =
    toplevel->scene_tree->node.x +
    toplevel->xdg_toplevel->base->geometry.x;
  uint32_t toplevel_y =
    toplevel->scene_tree->node.y +
    toplevel->xdg_toplevel->base->geometry.y;

  struct wlr_box toplevel_box = {
    .x = toplevel_x,
    .y = toplevel_y,
    .width = WIDTH(toplevel),
    .height = HEIGHT(toplevel),
  };

  struct wlr_box intersection_box;
  struct wlr_box output_box;
  uint32_t max_area = 0;
  struct owl_output *max_area_output = NULL;

  struct owl_output *o;
  wl_list_for_each(o, &server.outputs, link) {
    wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);
    bool intersects =
      wlr_box_intersection(&intersection_box, &toplevel_box, &output_box);
    if(intersects && box_area(&intersection_box) > max_area) {
      max_area = box_area(&intersection_box);
      max_area_output = o;
    }
  }

  return max_area_output;
}

static void
toplevel_clip_to_size(struct owl_toplevel *toplevel,
                      uint32_t width, uint32_t height) {
  struct wlr_box clip_box = (struct wlr_box){
    .x = 0,
    .y = 0,
    .width = width + toplevel->xdg_toplevel->base->geometry.x,
    .height = height + toplevel->xdg_toplevel->base->geometry.y,
  };

  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);
}

static void
toplevel_clip_to_fit(struct owl_toplevel *toplevel) {
  /* we only clip tiled toplevels if they are too big to fit the layout */
  assert(!toplevel->floating);

  struct wlr_box clip_box = (struct wlr_box){
    .x = 0,
    .y = 0,
    .width = toplevel->pending.width + toplevel->xdg_toplevel->base->geometry.x,
    .height = toplevel->pending.height + toplevel->xdg_toplevel->base->geometry.y,
  };

  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);
}

static void
toplevel_unclip_size(struct owl_toplevel *toplevel) {
  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, NULL);
}

static uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor,
                            struct owl_toplevel *toplevel) {
  struct wlr_box geometry = toplevel->xdg_toplevel->base->geometry;

  uint32_t toplevel_x = toplevel->scene_tree->node.x + geometry.x;
  uint32_t toplevel_y = toplevel->scene_tree->node.y + geometry.y;

  uint32_t left_dist = cursor->x - toplevel_x;
  uint32_t right_dist = geometry.width - left_dist;
  uint32_t top_dist = cursor->y - toplevel_y;
  uint32_t bottom_dist = geometry.height - top_dist;

  uint32_t edges = 0;
  if(left_dist <= right_dist) {
    edges |= WLR_EDGE_LEFT;
  } else {
    edges |= WLR_EDGE_RIGHT;
  }

  if(top_dist <= bottom_dist) {
    edges |= WLR_EDGE_TOP;
  } else {
    edges |= WLR_EDGE_BOTTOM;
  }

  return edges;
}

static bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel) {
  double animation_passed = calculate_animation_passed(&toplevel->animation);

  uint32_t width = toplevel->animation.initial_geometry.width +
    (toplevel->pending.width - toplevel->animation.initial_geometry.width) * animation_passed;
  uint32_t height = toplevel->animation.initial_geometry.height +
    (toplevel->pending.height - toplevel->animation.initial_geometry.height) * animation_passed;

  if(width > WIDTH(toplevel) || height > HEIGHT(toplevel)) {
    struct wlr_scene_buffer *scene_buffer = surface_find_buffer(&toplevel->scene_tree->node,
                                                                toplevel->xdg_toplevel->base->surface);
    wlr_scene_buffer_set_dest_size(scene_buffer, width, height);
  } else {
    toplevel_clip_to_size(toplevel, width, height); 
  }

  toplevel_borders_set_size(toplevel, width, height);

  uint32_t x = toplevel->animation.initial_geometry.x +
    (toplevel->pending.x - toplevel->animation.initial_geometry.x) * animation_passed;
  uint32_t y = toplevel->animation.initial_geometry.y +
    (toplevel->pending.y - toplevel->animation.initial_geometry.y) * animation_passed;

  if(toplevel->animation.passed_frames == 0) {
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
  }

  wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);

  toplevel->animation.current_geometry = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  return animation_passed == 1.0;
}

static bool
layout_tiled_ready(struct owl_workspace *workspace) {
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped || t->dirty) return false;
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped || t->dirty) return false;
  }

  return true;
}

static void
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

static void
toplevel_commit(struct owl_toplevel *toplevel) {
  if(toplevel->animation.should_animate) {
    if(toplevel->animation.running) {
      /* if there is already an animation running, we start this one from the current state */
      toplevel->animation.initial_geometry.width = toplevel->animation.current_geometry.width;
      toplevel->animation.initial_geometry.height = toplevel->animation.current_geometry.height;
    }
    toplevel->animation.passed_frames = 0;
    toplevel->animation.total_frames = server.config->animation_duration
      / frame_duration(toplevel->workspace->output->wlr_output->refresh);

    toplevel->animation.running = true;
    toplevel->animation.should_animate = false;
    wlr_output_schedule_frame(toplevel->workspace->output->wlr_output);
  } else {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->pending.x,
                                toplevel->pending.y);
    toplevel_borders_update(toplevel);
  }
}

static void
layout_commit(struct owl_workspace *workspace) {
  if(workspace->fullscreen_toplevel != NULL) return;

  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->masters, link) {
    toplevel_commit(t);
  }

  wl_list_for_each(t, &workspace->slaves, link) {
    toplevel_commit(t);
  }
}

static void
layout_send_configure(struct owl_workspace *workspace) {
  /* if there is a fullscreened toplevel we just skip */
  if(workspace->fullscreen_toplevel != NULL) return;
  /* if there are no masters we are done */
  if(wl_list_empty(&workspace->masters)) return;

  struct owl_output *output = workspace->output;

  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;
  double border_width = server.config->border_width;

  uint32_t slave_count = wl_list_length(&workspace->slaves);
  uint32_t master_count = wl_list_length(&workspace->masters);

  uint32_t master_width, master_height;
  calculate_masters_dimensions(output, master_count, slave_count,
                               &master_width, &master_height);

  struct owl_toplevel *m;
  size_t i = 0;
  wl_list_for_each(m, &workspace->masters, link) {
    uint32_t master_x = output->usable_area.x + outer_gaps
      + (master_width + 2 * border_width) * i
      + 2 * inner_gaps * i
      + border_width;
    uint32_t master_y = output->usable_area.y + outer_gaps
      + border_width;

    if(m->mapped) {
      toplevel_set_pending_state(m, master_x, master_y, master_width, master_height);
    } else {
      toplevel_set_initial_state(m, master_x, master_y, master_width, master_height);
    }
    i++;
  }

  if(slave_count == 0) return;

  /* share the remaining space among slaves */
  uint32_t slave_width, slave_height, slave_x, slave_y;
  calculate_slaves_dimensions(workspace->output, slave_count,
                              &slave_width, &slave_height);

  struct owl_toplevel *s;
  i = 0;
  wl_list_for_each(s, &workspace->slaves, link) {
    slave_x = output->usable_area.x + output->usable_area.width * master_ratio
      + inner_gaps + border_width;
    slave_y = output->usable_area.y + outer_gaps
      + i * (slave_height + inner_gaps * 2 + 2 * border_width)
      + border_width;

    if(s->mapped) {
      toplevel_set_pending_state(s, slave_x, slave_y, slave_width, slave_height);
    } else {
      toplevel_set_initial_state(s, slave_x, slave_y, slave_width, slave_height);
    }
    i++;
  }
}

/* this function assumes they are in the same workspace and
 * that t2 comes after t1 if in the same list */
static void
layout_swap_tiled_toplevels(struct owl_toplevel *t1,
                            struct owl_toplevel *t2) {
  struct wl_list *before_t1 = t1->link.prev;
  wl_list_remove(&t1->link);
  wl_list_insert(&t2->link, &t1->link);
  wl_list_remove(&t2->link);
  wl_list_insert(before_t1, &t2->link);

  layout_send_configure(t1->workspace);
}

static void
toplevel_set_fullscreen(struct owl_toplevel *toplevel) {
  if(!toplevel->mapped) return;

  if(toplevel->workspace->fullscreen_toplevel != NULL) return;

  struct owl_workspace *workspace = toplevel->workspace;
  struct owl_output *output = workspace->output;

  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout,
                            output->wlr_output, &output_box);

  toplevel->prev_geometry = (struct wlr_box){
    .x = X(toplevel),
    .y = Y(toplevel),
    .width = WIDTH(toplevel),
    .height = HEIGHT(toplevel),
  };

  workspace->fullscreen_toplevel = toplevel;
  toplevel->fullscreen = true;

  toplevel_borders_set_state(toplevel, OWL_BORDER_INVISIBLE);

  wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
  toplevel_set_pending_state(toplevel, output_box.x, output_box.y,
                             output_box.width, output_box.height);
  wlr_scene_node_reparent(&toplevel->scene_tree->node, server.fullscreen_tree);
}

static void
toplevel_unset_fullscreen(struct owl_toplevel *toplevel) {
  if(toplevel->workspace->fullscreen_toplevel != toplevel) return;

  struct owl_workspace *workspace = toplevel->workspace;
  struct owl_output *output = workspace->output;

  workspace->fullscreen_toplevel = NULL;
  toplevel->fullscreen = false;

  wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);

  if(server.focused_toplevel == toplevel) {
    toplevel_borders_set_state(toplevel, OWL_BORDER_ACTIVE);
  } else {
    toplevel_borders_set_state(toplevel, OWL_BORDER_INACTIVE);
  }

  if(toplevel->floating) {
    toplevel_set_pending_state(toplevel,
                               toplevel->prev_geometry.x, toplevel->prev_geometry.y,
                               toplevel->prev_geometry.width, toplevel->prev_geometry.height);
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
  } else {
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
  }

  layout_send_configure(workspace);
}

static void
server_change_workspace(struct owl_workspace *workspace,
                        bool keep_focus) {
  /* if it is the same as global active workspace, do nothing */
  if(server.active_workspace == workspace) return;

  /* if it is an already active on its output, just switch to it */
  if(workspace == workspace->output->active_workspace) {
    server.active_workspace = workspace;
    cursor_jump_output(workspace->output);
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
    /* we dont want to keep focus only if he is going to be under a fullscreen toplevel */
    if(workspace->fullscreen_toplevel != NULL) {
      focus_toplevel(workspace->fullscreen_toplevel);
    } else if(keep_focus) {
      return;
    } else if(!wl_list_empty(&workspace->masters)) {
      struct owl_toplevel *t = wl_container_of(workspace->masters.next, t, link);
      focus_toplevel(t);
    } else if(!wl_list_empty(&workspace->floating_toplevels)) {
      struct owl_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
      focus_toplevel(t);
    } else {
      unfocus_focused_toplevel();
    }
    return;
  }

  /* else remove all the toplevels on that workspace */
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->output->active_workspace->floating_toplevels, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }
  wl_list_for_each(t, &workspace->output->active_workspace->masters, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }
  wl_list_for_each(t, &workspace->output->active_workspace->slaves, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }

  /* and show this workspace's toplevels */
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }
  wl_list_for_each(t, &workspace->masters, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }

  if(server.active_workspace->output != workspace->output) {
    cursor_jump_output(workspace->output);
  }

  server.active_workspace = workspace;
  workspace->output->active_workspace = workspace;

  ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

  /* same as above */
  if(workspace->fullscreen_toplevel != NULL) {
    focus_toplevel(workspace->fullscreen_toplevel);
  } else if(keep_focus) {
    return;
  } else if(!wl_list_empty(&workspace->masters)) {
    struct owl_toplevel *t = wl_container_of(workspace->masters.next, t, link);
    focus_toplevel(t);
  } else if(!wl_list_empty(&workspace->floating_toplevels)) {
    struct owl_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
    focus_toplevel(t);
  } else {
    unfocus_focused_toplevel();
  }
}

static void
toplevel_move_to_workspace(struct owl_toplevel *toplevel,
                           struct owl_workspace *workspace) {
  assert(toplevel != NULL && workspace != NULL);
  if(toplevel->workspace == workspace) return;

  struct owl_workspace *old_workspace = toplevel->workspace;

  /* handle server state; note: even tho fullscreen toplevel is handled differently
   * we will still update its underlying type */
  if(toplevel->floating) {
    toplevel->workspace = workspace;
    wl_list_remove(&toplevel->link);
    wl_list_insert(&workspace->floating_toplevels, &toplevel->link);
  } else if(toplevel_is_master(toplevel)){
    wl_list_remove(&toplevel->link);
    if(!wl_list_empty(&old_workspace->slaves)) {
      struct owl_toplevel *s = wl_container_of(old_workspace->slaves.next, s, link);
      wl_list_remove(&s->link);
      wl_list_insert(old_workspace->masters.prev, &s->link);
    }

    toplevel->workspace = workspace;
    if(wl_list_length(&workspace->masters) < server.config->master_count) {
      wl_list_insert(workspace->masters.prev, &toplevel->link);
    } else {
      wl_list_insert(workspace->slaves.prev, &toplevel->link);
    }
  } else {
    wl_list_remove(&toplevel->link);

    toplevel->workspace = workspace;
    if(wl_list_length(&workspace->masters) < server.config->master_count) {
      wl_list_insert(workspace->masters.prev, &toplevel->link);
    } else {
      wl_list_insert(workspace->slaves.prev, &toplevel->link);
    }
  }

  /* handle rendering */
  if(toplevel->fullscreen) {
    old_workspace->fullscreen_toplevel = NULL;
    workspace->fullscreen_toplevel = toplevel;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, workspace->output->wlr_output, &output_box);
    toplevel_set_pending_state(toplevel, output_box.x, output_box.y,
                               output_box.width, output_box.height);

    if(toplevel->floating) {
      /* calculate where the toplevel should be placed after exiting fullscreen,
       * see note for floating bellow */
      uint32_t old_output_relative_x =
        toplevel->prev_geometry.x - old_workspace->output->usable_area.x;
      double relative_x =
        (double)old_output_relative_x / old_workspace->output->usable_area.width;

      uint32_t old_output_relative_y =
        toplevel->prev_geometry.y - old_workspace->output->usable_area.y;
      double relative_y =
        (double)old_output_relative_y / old_workspace->output->usable_area.height;

      uint32_t new_output_x = workspace->output->usable_area.x
        + relative_x * workspace->output->usable_area.width;
      uint32_t new_output_y = workspace->output->usable_area.y
        + relative_y * workspace->output->usable_area.height;

      toplevel->prev_geometry.x = new_output_x;
      toplevel->prev_geometry.y = new_output_y;
    } else {
      layout_send_configure(old_workspace);
    }
  } else if(toplevel->floating && old_workspace->output != workspace->output) {
    /* we want to place the toplevel to the same relative coordinates,
     * as the new output may have a different resolution */
    uint32_t old_output_relative_x =
      toplevel->scene_tree->node.x - old_workspace->output->usable_area.x;
    double relative_x =
      (double)old_output_relative_x / old_workspace->output->usable_area.width;

    uint32_t old_output_relative_y =
      toplevel->scene_tree->node.y - old_workspace->output->usable_area.y;
    double relative_y =
      (double)old_output_relative_y / old_workspace->output->usable_area.height;

    uint32_t new_output_x = workspace->output->usable_area.x
      + relative_x * workspace->output->usable_area.width;
    uint32_t new_output_y = workspace->output->usable_area.y
      + relative_y * workspace->output->usable_area.height;

    toplevel_set_pending_state(toplevel, new_output_x, new_output_y,
                               WIDTH(toplevel), HEIGHT(toplevel));
  } else {
    layout_send_configure(old_workspace);
    layout_send_configure(workspace);
  }

  /* change active workspace */
  server_change_workspace(workspace, true);
}

static struct owl_something *
something_at(double lx, double ly,
             struct wlr_surface **surface,
             double *sx, double *sy) {
  /* this returns the topmost node in the scene at the given layout coords */
  struct wlr_scene_node *node =
    wlr_scene_node_at(&server.scene->tree.node, lx, ly, sx, sy);
  if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
    return NULL;
  }

  struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
  struct wlr_scene_surface *scene_surface =
    wlr_scene_surface_try_from_buffer(scene_buffer);
  if (!scene_surface) {
    return NULL;
  }

  *surface = scene_surface->surface;

  struct wlr_scene_tree *tree = node->parent;
  struct owl_something *something = tree->node.data;
  while(something == NULL || something->type == OWL_POPUP) {
    tree = tree->node.parent;
    something = tree->node.data;
  }

  return something;
}

static bool
server_handle_keybinds(struct owl_keyboard *keyboard,
                       uint32_t keycode,
                       enum wl_keyboard_key_state state) {
  uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
  /* we create new empty state so we can get raw, unmodified key.
   * this is used becuase we already handle modifiers explicitly,
   * and dont want them to interfere. for example, shift would make it
   * harder to specify the right key e.g. we would have to write
   *   keybind alt+shift # <do_something>
   * instead of
   *   alt+shift 3 <do_something> */
  struct xkb_state *empty = xkb_state_new(keyboard->wlr_keyboard->keymap);
  xkb_keysym_t sym = xkb_state_key_get_one_sym(empty, keycode);
  xkb_state_unref(empty);

  struct keybind *k;
  wl_list_for_each(k, &server.config->keybinds, link) {
    if(k->active && k->stop && sym == k->sym
      && state == WL_KEYBOARD_KEY_STATE_RELEASED) {
      k->active = false;
      k->stop(k->args);
      return true;
    }

    if(modifiers == k->modifiers && sym == k->sym
      && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      k->active = true;
      k->action(k->args);
      return true;
    }
  }

  return false;
}

static void
keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
  /* This event is raised when a modifier key, such as shift or alt, is
   * pressed. We simply communicate this to the client. */
  struct owl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
  /*
   * A seat can only have one keyboard, but this is a limitation of the
   * Wayland protocol - not wlroots. We assign all connected keyboards to the
   * same seat. You can swap out the underlying wlr_keyboard like this and
   * wlr_seat handles this transparently.
   */
  wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);
  /* Send modifiers to the client. */
  wlr_seat_keyboard_notify_modifiers(server.seat, &keyboard->wlr_keyboard->modifiers);
}

static void
keyboard_handle_key(struct wl_listener *listener, void *data) {
  struct owl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct wlr_keyboard_key_event *event = data;

  /* translate libinput keycode -> xkbcommon */
  uint32_t keycode = event->keycode + 8;

  /* get a list of keysyms based on the keymap for this keyboard */
  const xkb_keysym_t *syms;
  xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

  bool handled = server_handle_keybinds(keyboard, keycode, event->state);

  if(!handled) {
    /* otherwise, we pass it along to the client */
    wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_key(server.seat,
                                 event->time_msec,
                                 event->keycode,
                                 event->state);
  }
}

static void
keyboard_handle_destroy(struct wl_listener *listener, void *data) {
  struct owl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

  wl_list_remove(&keyboard->modifiers.link);
  wl_list_remove(&keyboard->key.link);
  wl_list_remove(&keyboard->destroy.link);
  wl_list_remove(&keyboard->link);
  free(keyboard);
}

static void
server_handle_new_keyboard(struct wlr_input_device *device) {
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

  struct owl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
  keyboard->wlr_keyboard = wlr_keyboard;

  /* we need to prepare an XKB keymap and assign it to the keyboard. this
   * assumes the defaults (e.g. layout = "us"). */
  struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
                                                        XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_set_keymap(wlr_keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);

  uint32_t rate = server.config->keyboard_rate;
  uint32_t delay = server.config->keyboard_delay;
  wlr_keyboard_set_repeat_info(wlr_keyboard, rate, delay);

  keyboard->modifiers.notify = keyboard_handle_modifiers;
  wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
  keyboard->key.notify = keyboard_handle_key;
  wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
  keyboard->destroy.notify = keyboard_handle_destroy;
  wl_signal_add(&device->events.destroy, &keyboard->destroy);

  wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);

  wl_list_insert(&server.keyboards, &keyboard->link);
}

static void
server_handle_new_pointer(struct wlr_input_device *device) {
  /* enable natural scrolling and tap to click*/
  if(wlr_input_device_is_libinput(device)) {
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);

    if(libinput_device_config_scroll_has_natural_scroll(libinput_device)
      && server.config->natural_scroll) {
      libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, true);
    }
    if(libinput_device_config_tap_get_finger_count(libinput_device)
      && server.config->tap_to_click) {
      libinput_device_config_tap_set_enabled(libinput_device, true);
    }
  }

  wlr_cursor_attach_input_device(server.cursor, device);
}

static void
server_handle_new_input(struct wl_listener *listener, void *data) {
  struct wlr_input_device *input = data;

  switch(input->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      server_handle_new_keyboard(input);
      break;
    case WLR_INPUT_DEVICE_POINTER:
      server_handle_new_pointer(input);
      break;
    default:
      /* owl doesnt support touch devices, drawing tablets etc */
      break;
  }

  /* we need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&server.keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(server.seat, caps);
}

static void 
server_handle_request_cursor(struct wl_listener *listener, void *data) {
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat_client *focused_client = server.seat->pointer_state.focused_client;
  if(focused_client == event->seat_client) {
    /* once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. it will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs */
    wlr_cursor_set_surface(server.cursor, event->surface,
                           event->hotspot_x, event->hotspot_y);
    /* TODO: maybe this should be placed elsewhere */
    server.client_cursor.surface = event->surface;
    server.client_cursor.hotspot_x = event->hotspot_x;
    server.client_cursor.hotspot_y = event->hotspot_y;
  }
}

static void
server_handle_request_set_selection(struct wl_listener *listener, void *data) {
  /* this event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in owl we always honor
   */
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server.seat, event->source, event->serial);
}

/* TODO: probably should change this so it does less */
static void
server_reset_cursor_mode() {
  /* reset the cursor mode to passthrough. */
  server.cursor_mode = OWL_CURSOR_PASSTHROUGH;
  server.grabbed_toplevel->resizing = false;
  server.grabbed_toplevel = NULL;

  if(server.client_cursor.surface != NULL) {
    wlr_cursor_set_surface(server.cursor, server.client_cursor.surface,
                           server.client_cursor.hotspot_x, server.client_cursor.hotspot_y);
  } else {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
  }
}

static void
process_toplevel_move(uint32_t time) {
  /* move the grabbed toplevel to the new position */
  struct owl_toplevel *toplevel = server.grabbed_toplevel;
  struct wlr_box geometry = toplevel->xdg_toplevel->base->geometry;

  int new_x = server.grabbed_toplevel_initial_box.x + (server.cursor->x - server.grab_x);
  int new_y = server.grabbed_toplevel_initial_box.y + (server.cursor->y - server.grab_y);

  toplevel_set_pending_state(toplevel, new_x - geometry.x, new_y - geometry.y,
                             geometry.width, geometry.height);
}

static void
process_toplevel_resize(uint32_t time) {
  struct owl_toplevel *toplevel = server.grabbed_toplevel;

  toplevel->resizing = true;

  int start_x = server.grabbed_toplevel_initial_box.x;
  int start_y = server.grabbed_toplevel_initial_box.y;
  int start_width = server.grabbed_toplevel_initial_box.width;
  int start_height = server.grabbed_toplevel_initial_box.height;

  int new_x = server.grabbed_toplevel_initial_box.x;
  int new_y = server.grabbed_toplevel_initial_box.y;
  int new_width = server.grabbed_toplevel_initial_box.width;
  int new_height = server.grabbed_toplevel_initial_box.height;

  int min_width = max(toplevel->xdg_toplevel->current.min_width,
                      server.config->min_toplevel_size);
  int min_height = max(toplevel->xdg_toplevel->current.min_height,
                       server.config->min_toplevel_size);

  if(server.resize_edges & WLR_EDGE_TOP) {
    new_y = start_y + (server.cursor->y - server.grab_y);
    new_height = start_height - (server.cursor->y - server.grab_y);
    if(new_height <= min_height) {
      new_y = start_y + start_height - min_height;
      new_height = min_height;
    }
  } else if(server.resize_edges & WLR_EDGE_BOTTOM) {
    new_y = start_y;
    new_height = start_height + (server.cursor->y - server.grab_y);
    if(new_height <= min_height) {
      new_height = min_height;
    }
  }
  if(server.resize_edges & WLR_EDGE_LEFT) {
    new_x = start_x + (server.cursor->x - server.grab_x);
    new_width = start_width - (server.cursor->x - server.grab_x);
    if(new_width <= min_width) {
      new_x = start_x + start_width - min_width;
      new_width = min_width;
    }
  } else if(server.resize_edges & WLR_EDGE_RIGHT) {
    new_x = start_x;
    new_width = start_width + (server.cursor->x - server.grab_x);
    if(new_width <= min_width) {
      new_width = min_width;
    }
  }

  struct wlr_box *geometry = &toplevel->xdg_toplevel->base->geometry;
  toplevel_set_pending_state(toplevel,
                             new_x - geometry->x, new_y - geometry->y,
                             new_width, new_height);
}

static void
process_cursor_motion(uint32_t time) {
  /* get the output that the cursor is on currently */
  struct wlr_output *wlr_output = wlr_output_layout_output_at(
    server.output_layout, server.cursor->x, server.cursor->y);
  struct owl_output *output = wlr_output->data;

  /* set global active workspace */
  if(output->active_workspace != server.active_workspace) {
    server.active_workspace = output->active_workspace;
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
  }

  if(server.cursor_mode == OWL_CURSOR_MOVE) {
    process_toplevel_move(time);
    return;
  } else if (server.cursor_mode == OWL_CURSOR_RESIZE) {
    process_toplevel_resize(time);
    return;
  }

  /* find something under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server.seat;
  struct wlr_surface *surface = NULL;
  struct owl_something *something =
    something_at(server.cursor->x, server.cursor->y, &surface, &sx, &sy);

  if(something == NULL) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
    /* clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it */
    wlr_seat_pointer_clear_focus(seat);
    return;
  }

  if(something->type == OWL_TOPLEVEL) {
    focus_toplevel(something->toplevel);
  } else {
    focus_layer_surface(something->layer_surface);
  }

  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

static void
server_handle_cursor_motion(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_event *event = data;
  wlr_cursor_move(server.cursor, &event->pointer->base,
                  event->delta_x, event->delta_y);
  process_cursor_motion(event->time_msec);
}


static void
server_handle_cursor_motion_absolute(
  struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_absolute_event *event = data;
  wlr_cursor_warp_absolute(server.cursor, &event->pointer->base, event->x, event->y);
  process_cursor_motion(event->time_msec);
}

/* TODO: this needs fixing, also add mouse button shortcuts */
static void
server_handle_cursor_button(struct wl_listener *listener, void *data) {
  struct wlr_pointer_button_event *event = data;

  /* notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server.seat, event->time_msec,
                                 event->button, event->state);

  if(event->state == WL_POINTER_BUTTON_STATE_RELEASED
    && server.cursor_mode != OWL_CURSOR_PASSTHROUGH) {
    struct owl_output *primary_output = 
      toplevel_get_primary_output(server.grabbed_toplevel);

    if(primary_output != server.grabbed_toplevel->workspace->output) {
      server.grabbed_toplevel->workspace = primary_output->active_workspace;
      wl_list_remove(&server.grabbed_toplevel->link);
      wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                     &server.grabbed_toplevel->link);
    }

    server_reset_cursor_mode();
  }
}


static void
server_handle_cursor_axis(struct wl_listener *listener, void *data) {
  struct wlr_pointer_axis_event *event = data;

  /* notify the client with pointer focus of the axis event */
  wlr_seat_pointer_notify_axis(server.seat,
                               event->time_msec, event->orientation, event->delta,
                               event->delta_discrete, event->source, event->relative_direction);
}

static void
server_handle_cursor_frame(struct wl_listener *listener, void *data) {
  wlr_seat_pointer_notify_frame(server.seat);
}

static struct timespec last;
static void
output_handle_frame(struct wl_listener *listener, void *data) {
  /* this function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate */
  struct owl_output *output = wl_container_of(listener, output, frame);
  struct owl_workspace *workspace = output->active_workspace;

  bool animations_done = true;
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(!t->mapped) continue;
    if(t->animation.running) {
      bool done = toplevel_animation_next_tick(t);
      if(done) {
        t->animation.running = false;
      } else {
        t->animation.passed_frames++;
        animations_done = false;
      }
    } else {
      wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    }
  }
  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped) continue;
    if(t->animation.running) {
      bool done = toplevel_animation_next_tick(t);
      if(done) {
        t->animation.running = false;
      } else {
        t->animation.passed_frames++;
        animations_done = false;
      }
    } else {
      wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    }
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped) continue;
    if(t->animation.running) {
      bool done = toplevel_animation_next_tick(t);
      if(done) {
        t->animation.running = false;
      } else {
        t->animation.passed_frames++;
        animations_done = false;
      }
    } else {
      wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    }
  }

  struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server.scene,
                                                                     output->wlr_output);
  wlr_scene_output_commit(scene_output, NULL);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  wlr_scene_output_send_frame_done(scene_output, &now);

  if(!animations_done) {
    wlr_output_schedule_frame(output->wlr_output);
  }
}

static void
output_handle_request_state(struct wl_listener *listener, void *data) {
  /* this function is called when the backend requests a new state for
   * the output. for example, wayland and X11 backends request a new mode
   * when the output window is resized */
  struct owl_output *output = wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;
  wlr_output_commit_state(output->wlr_output, event->state);
}

/* TODO: this needs tweaking in the future, rn outputs are not removed from
 * the layout, and workspaces and not updated. */
static void
output_handle_destroy(struct wl_listener *listener, void *data) {
  struct owl_output *output = wl_container_of(listener, output, destroy);

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->link);
  free(output);
}

/* forward declaration so i can keep all keybind related stuff down there */
static void
keybind_change_workspace(void *data);
static void
keybind_move_focused_toplevel_to_workspace(void *data);

static void
server_handle_new_output(struct wl_listener *listener, void *data) {
  struct wlr_output *wlr_output = data;

  wlr_output_init_render(wlr_output, server.allocator, server.renderer);

  /* the output may be disabled, switch it on */
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);

  /* we try to find the config for this output */
  struct output_config *output_config = NULL;

  struct output_config *o;
  wl_list_for_each(o, &server.config->outputs, link) {
    if(strcmp(o->name, wlr_output->name) == 0) {
      output_config = o;
      break;
    }
  }

  if(output_config != NULL) {
    /* we try to find the closest supported mode for this output, that means:
     *  - same resolution
     *  - closest refresh rate
     * if there is none we take the prefered mode for the output */
    struct wlr_output_mode *best_match = NULL;
    uint32_t best_match_diff = UINT32_MAX;

    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
      if(m->width == o->width && m->height == o->height
        && abs((int)m->refresh - (int)o->refresh_rate) < best_match_diff) {
        best_match = m;
        best_match_diff = abs((int)m->refresh - (int)o->refresh_rate);
      }
    }

    if(best_match != NULL) {
      wlr_log(WLR_INFO, "trying to set mode for output %s to %dx%d@%dmHz",
              wlr_output->name, best_match->width, best_match->height, best_match->refresh);
      /* we set the mode and try to commit the state.
       * if it fails then we backup to the preffered. it should not fail! */
      wlr_output_state_set_mode(&state, best_match);
      bool success = wlr_output_commit_state(wlr_output, &state);
      if(!success) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
        if(mode != NULL) {
          wlr_output_state_set_mode(&state, mode);
          wlr_log(WLR_ERROR, "couldn't find a mode to set to the output %s", wlr_output->name);
          /* free the resource */
          wlr_output_state_finish(&state);
          return;
        }
        success = wlr_output_commit_state(wlr_output, &state);
        if(!success) {
          wlr_log(WLR_ERROR, "couldn't find a mode to set to the output %s", wlr_output->name);
          /* free the resource */
          wlr_output_state_finish(&state);
          return;
        }
      }
    } else {

    }
  } else {
    /* if it is not specified in the config we take its preffered mode */
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if(mode == NULL) {
      wlr_log(WLR_ERROR, "couldn't find a mode to set to the output %s", wlr_output->name);
      /* free the resource */
      wlr_output_state_finish(&state);
      return;
    }
    wlr_output_state_set_mode(&state, mode);
    bool success = wlr_output_commit_state(wlr_output, &state);
    if(!success) {
      wlr_log(WLR_ERROR, "couldn't find a mode to set to the output %s", wlr_output->name);
      /* free the resource */
      wlr_output_state_finish(&state);
      return;
    }
  }

  wlr_log(WLR_INFO, "successfully set up output %s", wlr_output->name);
  wlr_output_state_finish(&state);

  /* allocates and configures our state for this output */
  struct owl_output *output = calloc(1, sizeof(*output));
  output->wlr_output = wlr_output;

  wlr_output->data = output;

  output->frame.notify = output_handle_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);

  output->request_state.notify = output_handle_request_state;
  wl_signal_add(&wlr_output->events.request_state, &output->request_state);

  output->destroy.notify = output_handle_destroy;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);

  wl_list_init(&output->workspaces);

  struct workspace_config *w;
  wl_list_for_each_reverse(w, &server.config->workspaces, link) {
    if(strcmp(w->output, wlr_output->name) == 0) {
      struct owl_workspace *workspace = calloc(1, sizeof(*workspace));
      wl_list_init(&workspace->floating_toplevels);
      wl_list_init(&workspace->masters);
      wl_list_init(&workspace->slaves);
      workspace->output = output;
      workspace->index = w->index;

      wl_list_insert(&output->workspaces, &workspace->link);

      /* if first then set it active */
      if(output->active_workspace == NULL) {
        output->active_workspace = workspace;
      }

      struct keybind *k;
      wl_list_for_each(k, &server.config->keybinds, link) {
        /* we didnt have information about what workspace this is going to be,
         * so we only kept an index. now we replace it with
         * the actual workspace pointer */
        if(k->action == keybind_change_workspace
          && (uint32_t)k->args == workspace->index) {
          k->args = workspace;
        } else if(k->action == keybind_move_focused_toplevel_to_workspace
          && (uint32_t)k->args == workspace->index) {
          k->args = workspace;
        }
      }
    }
  }

  /* if we didnt find any workspace config, then we give it workspace with index -1 */
  if(wl_list_empty(&output->workspaces)) {
    wlr_log(WLR_ERROR, "no workspace config specified for output %s."
            "using default workspace UINT32_MAX. please add a valid workspace config.",
            wlr_output->name);

    struct owl_workspace *workspace = calloc(1, sizeof(*workspace));
    wl_list_init(&workspace->floating_toplevels);
    wl_list_init(&workspace->masters);
    wl_list_init(&workspace->slaves);
    workspace->output = output;
    workspace->index = UINT32_MAX;

    wl_list_insert(&output->workspaces, &workspace->link);

    output->active_workspace = workspace;
  }

  wl_list_init(&output->layers.background);
  wl_list_init(&output->layers.bottom);
  wl_list_init(&output->layers.top);
  wl_list_init(&output->layers.overlay);

  wl_list_insert(&server.outputs, &output->link);

  struct wlr_output_layout_output *l_output;

  if(output_config != NULL) {
    wlr_log(WLR_INFO, "setting position of output %s to %d, %d",
            wlr_output->name, output_config->x, output_config->y);
    l_output = wlr_output_layout_add(server.output_layout, wlr_output,
                                     output_config->x, output_config->y);
  } else {
    l_output = wlr_output_layout_add_auto(server.output_layout, wlr_output);
  }

  struct wlr_scene_output *scene_output = wlr_scene_output_create(server.scene, wlr_output);

  wlr_scene_output_layout_add_output(server.scene_layout, l_output, scene_output);

  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, wlr_output, &output_box);
  output->usable_area = output_box;

  /* if first output then set server's active workspace to this one */
  if(server.active_workspace == NULL) {
    server.active_workspace = output->active_workspace;
  }
}

static void
xdg_toplevel_handle_map(struct wl_listener *listener, void *data) {
  /* called when the surface is mapped, or ready to display on-screen. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, map);

  toplevel->mapped = true;
  toplevel->dirty = false;

  toplevel_borders_create(toplevel);
  focus_toplevel(toplevel);

  if(toplevel->floating) {
    /* we commit it immediately if floating, but have to set the position before */
    struct wlr_box output_box = toplevel->workspace->output->usable_area;
    toplevel->animation.initial_geometry.x = output_box.x + output_box.width / 2;
    toplevel->animation.initial_geometry.y = output_box.y + output_box.height / 2;
    toplevel_center_floating(toplevel);
    toplevel_commit(toplevel);
  } else {
    if(layout_tiled_ready(toplevel->workspace)) {
      layout_commit(toplevel->workspace);
    }
  }

  /* do the thing for foreign_toplevel_manager */
  toplevel->foreign_toplevel_handle
    = wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);
  wlr_foreign_toplevel_handle_v1_set_title(
    toplevel->foreign_toplevel_handle, toplevel->xdg_toplevel->title);
  wlr_foreign_toplevel_handle_v1_set_app_id(
    toplevel->foreign_toplevel_handle, toplevel->xdg_toplevel->app_id);
}

static void
xdg_toplevel_handle_unmap(struct wl_listener *listener, void *data) {
  /* called when the surface is unmapped, and should no longer be shown. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
  struct owl_workspace *workspace = toplevel->workspace;

  /* reset the cursor mode if the grabbed toplevel was unmapped. */
  if(toplevel == server.grabbed_toplevel) {
    server_reset_cursor_mode();
  }

  /* if its the one focus should be returned to, remove it */
  if(toplevel == server.prev_focused) {
    server.prev_focused = NULL;
  }

  if(toplevel == workspace->fullscreen_toplevel) {
    workspace->fullscreen_toplevel = NULL;
  }

  if(toplevel->floating) {
    if(server.focused_toplevel == toplevel) {
      /* try to find other floating toplevels to give focus to */
      struct wl_list *focus_next = toplevel->link.next;
      if(focus_next == &workspace->floating_toplevels) {
        focus_next = toplevel->link.prev;
        if(focus_next == &workspace->floating_toplevels) {
          focus_next = workspace->masters.next;
          if(focus_next == &workspace->masters) {
            focus_next = NULL;
          }
        }
      }

      if(focus_next != NULL) {
        struct owl_toplevel *t = wl_container_of(focus_next, t, link);
        focus_toplevel(t);
      } else {
        server.focused_toplevel = NULL;
      }
    }

    wl_list_remove(&toplevel->link);
    return;
  }

  if(toplevel_is_master(toplevel)) {
    if(toplevel == server.focused_toplevel) {
      /* we find a new master to replace him if possible */
      if(!wl_list_empty(&workspace->slaves)) {
        struct owl_toplevel *s = wl_container_of(workspace->slaves.next, s, link);
        wl_list_remove(&s->link);
        wl_list_insert(workspace->masters.prev, &s->link);
      }

      /* we want to give focus to some other toplevel */
      struct wl_list *focus_next = toplevel->link.next;
      if(focus_next == &workspace->masters) {
        focus_next = toplevel->link.prev;
        if(focus_next == &workspace->masters) {
          focus_next = workspace->floating_toplevels.next;
          if(focus_next == &workspace->floating_toplevels) {
            focus_next = NULL;
          }
        }
      }

      if(focus_next != NULL) {
        struct owl_toplevel *t = wl_container_of(focus_next, t, link);
        focus_toplevel(t);
      } else {
        server.focused_toplevel = NULL;
      }
    }

    /* we finally remove him from the list */
    wl_list_remove(&toplevel->link);
  } else {
    if(toplevel == server.focused_toplevel) {
      /* we want to give focus to some other toplevel */
      struct wl_list *focus_next = toplevel->link.next;
      if(focus_next == &workspace->slaves) {
        focus_next = toplevel->link.prev;
        if(focus_next == &workspace->slaves) {
          /* take the last master */
          focus_next = workspace->masters.prev;
        }
      }
      /* here its not possible to have no other toplevel to give focus,
       * there are always master_count masters available */
      struct owl_toplevel *t = wl_container_of(focus_next, t, link);
      focus_toplevel(t);
    }

    wl_list_remove(&toplevel->link);
  }

  layout_send_configure(toplevel->workspace);
}

static void
xdg_toplevel_handle_commit(struct wl_listener *listener, void *data) {
  /* called when a new surface state is committed */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

  if(toplevel->xdg_toplevel->base->initial_commit) {
    /* when an xdg_surface performs an initial commit, the compositor must
     * reply with a configure so the client can map the surface. */
    toplevel->workspace = server.active_workspace;
    toplevel->floating = toplevel_should_float(toplevel);

    if(toplevel->floating) {
      wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
      /* we lookup window rules and send a configure */
      uint32_t width, height;
      toplevel_floating_size(toplevel, &width, &height);
      toplevel_set_initial_state(toplevel, 0, 0, width, height);
    } else if(wl_list_length(&toplevel->workspace->masters)
      < server.config->master_count) {
      wl_list_insert(toplevel->workspace->masters.prev, &toplevel->link);
      layout_send_configure(toplevel->workspace);
    } else {
      wl_list_insert(toplevel->workspace->slaves.prev, &toplevel->link);
      layout_send_configure(toplevel->workspace);
    }

    if(server.config->animations) {
      toplevel->animation.should_animate = true;
    } else {
      toplevel->animation.should_animate = false;
    }
    return;
  }

  if(!toplevel->mapped) return;

  if(toplevel->resizing) {
    toplevel_commit(toplevel);
    return;
  }

  if(!toplevel->dirty || toplevel->xdg_toplevel->base->current.configure_serial
     != toplevel->configure_serial) return;

  toplevel->dirty = false;

  if(toplevel->fullscreen) {
    toplevel_commit(toplevel);
    return;
  }

  if(toplevel->floating) {
    if(toplevel->pending.x == 0) {
      toplevel_center_floating(toplevel);
    }
    if(toplevel->pending.width == 0) {
      toplevel->pending.width = WIDTH(toplevel);
      toplevel->pending.height = HEIGHT(toplevel);
    }
    toplevel_commit(toplevel);
    return;
  }

  if(layout_tiled_ready(toplevel->workspace)) {
    layout_commit(toplevel->workspace);
  }
}

static void
xdg_toplevel_handle_destroy(struct wl_listener *listener, void *data) {
  /* called when the xdg_toplevel is destroyed. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

  wl_list_remove(&toplevel->map.link);
  wl_list_remove(&toplevel->unmap.link);
  wl_list_remove(&toplevel->commit.link);
  wl_list_remove(&toplevel->destroy.link);
  wl_list_remove(&toplevel->request_move.link);
  wl_list_remove(&toplevel->request_resize.link);
  wl_list_remove(&toplevel->request_maximize.link);
  wl_list_remove(&toplevel->request_fullscreen.link);

  free(toplevel);
}

static void
server_start_toplevel_move_resize(struct owl_toplevel *toplevel,
                                  enum owl_cursor_mode mode, uint32_t edges) {
  if(toplevel == NULL) {
    return;
  }

  server.grabbed_toplevel = toplevel;
  server.cursor_mode = mode;

  server.grab_x = server.cursor->x;
  server.grab_y = server.cursor->y;

  struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
  server.grabbed_toplevel_initial_box = *geo_box;
  server.grabbed_toplevel_initial_box.x += toplevel->scene_tree->node.x;
  server.grabbed_toplevel_initial_box.y += toplevel->scene_tree->node.y;

  server.resize_edges = edges;
}

static void
xdg_toplevel_handle_request_move(
  struct wl_listener *listener, void *data) {
  /* This event is raised when a client would like to begin an interactive
   * move, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provided serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
  if(!toplevel->floating || toplevel != get_pointer_focused_toplevel()) return;

  server_start_toplevel_move_resize(toplevel, OWL_CURSOR_MOVE, 0);
}

static void
xdg_toplevel_handle_request_resize(
  struct wl_listener *listener, void *data) {
  /* This event is raised when a client would like to begin an interactive
   * resize, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provided serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct wlr_xdg_toplevel_resize_event *event = data;

  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
  if(!toplevel->floating || toplevel != get_pointer_focused_toplevel()) return;

  server_start_toplevel_move_resize(toplevel, OWL_CURSOR_RESIZE, event->edges);
}

static void
xdg_toplevel_handle_request_maximize(
  struct wl_listener *listener, void *data) {
  /* This event is raised when a client would like to maximize itself,
   * typically because the user clicked on the maximize button on client-side
   * decorations. owl doesn't support maximization, but to conform to
   * xdg-shell protocol we still must send a configure.
   * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
   * However, if the request was sent before an initial commit, we don't do
   * anything and let the client finish the initial surface setup. */
  struct owl_toplevel *toplevel =
    wl_container_of(listener, toplevel, request_maximize);
  if(toplevel->xdg_toplevel->base->initialized) {
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
  }
}

static void
xdg_toplevel_handle_request_fullscreen(
  struct wl_listener *listener, void *data) {
  struct owl_toplevel *toplevel =
    wl_container_of(listener, toplevel, request_fullscreen);

  struct owl_output *output = toplevel->workspace->output;
  if(toplevel->xdg_toplevel->requested.fullscreen) {
    toplevel_set_fullscreen(toplevel);
  } else {
    toplevel_unset_fullscreen(toplevel);
  }
}

static void
xdg_toplevel_handle_set_app_id(struct wl_listener *listener, void *data) {
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);

  if(toplevel == server.focused_toplevel) {
    ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
  }
}

static void
xdg_toplevel_handle_set_title(struct wl_listener *listener, void *data) {
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);

  if(toplevel == server.focused_toplevel) {
    ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
  }
}

static void
server_handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
  /* This event is raised when a client creates a new toplevel (application window). */
  struct wlr_xdg_toplevel *xdg_toplevel = data;

  /* Allocate a owl_toplevel for this surface */
  struct owl_toplevel *toplevel = calloc(1, sizeof(*toplevel));

  toplevel->xdg_toplevel = xdg_toplevel;

  /* Listen to the various events it can emit */
  toplevel->map.notify = xdg_toplevel_handle_map;
  wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

  toplevel->unmap.notify = xdg_toplevel_handle_unmap;
  wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);

  toplevel->commit.notify = xdg_toplevel_handle_commit;
  wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

  toplevel->destroy.notify = xdg_toplevel_handle_destroy;
  wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

  toplevel->request_move.notify = xdg_toplevel_handle_request_move;
  wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);

  toplevel->request_resize.notify = xdg_toplevel_handle_request_resize;
  wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);

  toplevel->request_maximize.notify = xdg_toplevel_handle_request_maximize;
  wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);

  toplevel->request_fullscreen.notify = xdg_toplevel_handle_request_fullscreen;
  wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);

  toplevel->set_app_id.notify = xdg_toplevel_handle_set_app_id;
  wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);

  toplevel->set_title.notify = xdg_toplevel_handle_set_title;
  wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}

static void
xdg_popup_handle_commit(struct wl_listener *listener, void *data) {
  /* Called when a new surface state is committed. */
  struct owl_popup *popup = wl_container_of(listener, popup, commit);

  if(!popup->xdg_popup->base->initialized) return;

  if(popup->xdg_popup->base->initial_commit) {
    /* When an xdg_surface performs an initial commit, the compositor must
     * reply with a configure so the client can map the surface.
     * owl sends an empty configure. A more sophisticated compositor
     * might change an xdg_popup's geometry to ensure it's not positioned
     * off-screen, for example. */
    struct owl_something *root = root_parent_of_surface(popup->xdg_popup->base->surface);

    if(root == NULL) {
      wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    } else if(root->type == OWL_TOPLEVEL) {
      struct wlr_box output_box = root->toplevel->workspace->output->usable_area;

      output_box.x -= root->toplevel->scene_tree->node.x;
      output_box.y -= root->toplevel->scene_tree->node.y;

      wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
    } else {
      struct owl_layer_surface *layer_surface= root->layer_surface;
      struct wlr_output *wlr_output = layer_surface->wlr_layer_surface->output;

      struct wlr_box output_box;
      wlr_output_layout_get_box(server.output_layout, wlr_output, &output_box);

      output_box.x -= layer_surface->scene->tree->node.x;
      output_box.y -= layer_surface->scene->tree->node.y;

      wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
    }
  }
}

static void
xdg_popup_handle_destroy(struct wl_listener *listener, void *data) {
  /* Called when the xdg_popup is destroyed. */
  struct owl_popup *popup = wl_container_of(listener, popup, destroy);

  wl_list_remove(&popup->commit.link);
  wl_list_remove(&popup->destroy.link);

  free(popup);
}

static void
server_handle_new_xdg_popup(struct wl_listener *listener, void *data) {
  /* this event is raised when a client creates a new popup */
  struct wlr_xdg_popup *xdg_popup = data;

  struct owl_popup *popup = calloc(1, sizeof(*popup));
  popup->xdg_popup = xdg_popup;

  if(xdg_popup->parent != NULL) {
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    struct wlr_scene_tree *parent_tree = parent->data;
    popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    xdg_popup->base->data = popup->scene_tree;
    struct owl_something *something = calloc(1, sizeof(*something));
    something->type = OWL_POPUP;
    something->popup = popup;
    popup->scene_tree->node.data = something;
  } else {
    /* if there is no parent, than we keep the reference to our owl_popup state in this */
    /* user data pointer, in order to later reparent this popup (see layer_surface_handle_new_popup) */
    xdg_popup->base->data = popup;
  }

  popup->commit.notify = xdg_popup_handle_commit;
  wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

  popup->destroy.notify = xdg_popup_handle_destroy;
  wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void
server_handle_request_xdg_decoration(struct wl_listener *listener, void *data) {
  struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
  wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                                          WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
layer_surface_handle_commit(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, commit);

  if(!layer_surface->wlr_layer_surface->initialized) {
    return;
  }

  if(layer_surface->wlr_layer_surface->initial_commit) {
    struct owl_output *output = layer_surface->wlr_layer_surface->output->data;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    struct wlr_box temp = output->usable_area;
    wlr_scene_layer_surface_v1_configure(layer_surface->scene, &output_box, &output->usable_area);
  }
}

static void
layer_surface_handle_map(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, map);
  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
  struct owl_output *output = wlr_layer_surface->output->data;

  wlr_scene_node_raise_to_top(&layer_surface->scene->tree->node);

  switch(layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      wl_list_insert(&output->layers.background, &layer_surface->link);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      wl_list_insert(&output->layers.bottom, &layer_surface->link);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      wl_list_insert(&output->layers.top, &layer_surface->link);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      wl_list_insert(&output->layers.overlay, &layer_surface->link);
      break;
  }

  int x, y;
  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  struct wlr_box temp = output->usable_area;
  wlr_scene_layer_surface_v1_configure(layer_surface->scene, &output_box, &output->usable_area);

  if(temp.width != output->usable_area.width || temp.height != output->usable_area.height) {
    layout_send_configure(output->active_workspace);
  }

  focus_layer_surface(layer_surface);
}

static void
layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, unmap);

  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;
  struct wlr_layer_surface_v1_state *state = &layer_surface->wlr_layer_surface->current;
  struct owl_output *output = layer_surface->wlr_layer_surface->output->data;

  if(layer_surface == server.layer_exclusive_keyboard) {
    server.layer_exclusive_keyboard = NULL;

    if(server.prev_focused != NULL) {
      focus_toplevel(server.prev_focused);
    }
  }

  if(layer_surface->wlr_layer_surface->current.exclusive_zone > 0) {
    switch (state->anchor) {
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor top
        output->usable_area.y -= state->exclusive_zone + state->margin.top;
        output->usable_area.height += state->exclusive_zone + state->margin.top;
        break;
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor bottom
        output->usable_area.height += state->exclusive_zone + state->margin.bottom;
        break;
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT):
        // Anchor left
        output->usable_area.x -= state->exclusive_zone + state->margin.left;
        output->usable_area.width += state->exclusive_zone + state->margin.left;
        break;
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor right
        output->usable_area.width += state->exclusive_zone + state->margin.right;
        break;
    }

    layout_send_configure(output->active_workspace);
  }

  wl_list_remove(&layer_surface->link);
}

static void
layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, destroy);

  wl_list_remove(&layer_surface->map.link);
  wl_list_remove(&layer_surface->unmap.link);
  wl_list_remove(&layer_surface->destroy.link);

  free(layer_surface);
}

static void
layer_surface_handle_new_popup(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, new_popup);
  struct wlr_xdg_popup *xdg_popup = data;

  /* see server_handle_new_xdg_popup */
  struct owl_popup *popup = xdg_popup->base->data;

  struct wlr_scene_tree *parent_tree = layer_surface->scene->tree;
  popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

  /* TODO: this is going to be used to get the layer surface from the popup */
  struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_POPUP;
  something->popup = popup;
  popup->scene_tree->node.data = something;

  popup->xdg_popup->base->data = popup->scene_tree;
}

static void
server_handle_new_layer_surface(struct wl_listener *listener, void *data) {
  struct wlr_layer_surface_v1 *wlr_layer_surface = data;

  struct owl_layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));
  layer_surface->wlr_layer_surface = wlr_layer_surface;
  layer_surface->wlr_layer_surface->data = layer_surface;

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
  switch(layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.background_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.bottom_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.top_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.overlay_tree, wlr_layer_surface);
      break;
  }

  struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_LAYER_SURFACE;
  something->layer_surface = layer_surface;

  layer_surface->scene->tree->node.data = something;

  if(layer_surface->wlr_layer_surface->output == NULL) {
    layer_surface->wlr_layer_surface->output = server.active_workspace->output->wlr_output;
  }

  layer_surface->commit.notify = layer_surface_handle_commit;
  wl_signal_add(&wlr_layer_surface->surface->events.commit, &layer_surface->commit);

  layer_surface->map.notify = layer_surface_handle_map;
  wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surface->map);

  layer_surface->unmap.notify = layer_surface_handle_unmap;
  wl_signal_add(&wlr_layer_surface->surface->events.unmap, &layer_surface->unmap);

  layer_surface->new_popup.notify = layer_surface_handle_new_popup;
  wl_signal_add(&wlr_layer_surface->events.new_popup, &layer_surface->new_popup);

  layer_surface->destroy.notify = layer_surface_handle_destroy;
  wl_signal_add(&wlr_layer_surface->surface->events.destroy, &layer_surface->destroy);
}

static void
keybind_stop_server(void *data) {
  wl_display_terminate(server.wl_display);
}

static void
keybind_run(void *data) {
  run_cmd(data);
}

static void
keybind_change_workspace(void *data) {
  struct owl_workspace *workspace = data;
  server_change_workspace(workspace, false);
}

static void
keybind_move_focused_toplevel_to_workspace(void *data) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  struct owl_workspace *workspace = data;
  toplevel_move_to_workspace(toplevel, workspace);
}

static void
keybind_resize_focused_toplevel(void *data) {
  struct owl_toplevel *toplevel = get_pointer_focused_toplevel();
  if(toplevel == NULL || !toplevel->floating) return;

  uint32_t edges = toplevel_get_closest_corner(server.cursor, toplevel);

  char cursor_image[128] = {0};
  if(edges & WLR_EDGE_TOP) {
    strcat(cursor_image, "top_");
  } else {
    strcat(cursor_image, "bottom_");
  }
  if(edges & WLR_EDGE_LEFT) {
    strcat(cursor_image, "left_");
  } else {
    strcat(cursor_image, "right_");
  }
  strcat(cursor_image, "corner");

  wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, cursor_image);
  server_start_toplevel_move_resize(toplevel, OWL_CURSOR_RESIZE, edges);
}

static void
keybind_stop_resize_focused_toplevel(void *data) {
  if(server.grabbed_toplevel == NULL) return;

  struct owl_output *primary_output = 
    toplevel_get_primary_output(server.grabbed_toplevel);
  if(primary_output != server.grabbed_toplevel->workspace->output) {
    server.grabbed_toplevel->workspace = primary_output->active_workspace;
    wl_list_remove(&server.grabbed_toplevel->link);
    wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                   &server.grabbed_toplevel->link);
  }

  server_reset_cursor_mode();
}

static void
keybind_move_focused_toplevel(void *data) {
  struct owl_toplevel *toplevel = get_pointer_focused_toplevel();
  if(toplevel == NULL || !toplevel->floating || toplevel->fullscreen) return;

  wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "hand1");
  server_start_toplevel_move_resize(toplevel, OWL_CURSOR_MOVE, 0);
}

static void
keybind_stop_move_focused_toplevel(void *data) {
  if(server.grabbed_toplevel == NULL) return;

  struct owl_output *primary_output = 
    toplevel_get_primary_output(server.grabbed_toplevel);
  if(primary_output != server.grabbed_toplevel->workspace->output) {
    server.grabbed_toplevel->workspace = primary_output->active_workspace;
    wl_list_remove(&server.grabbed_toplevel->link);
    wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                   &server.grabbed_toplevel->link);
  }

  server_reset_cursor_mode();
}

static void
keybind_close_keyboard_focused_toplevel(void *data) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  xdg_toplevel_send_close(toplevel->xdg_toplevel->resource);
}

static void keybind_move_focus(void *data) {
  uint32_t direction = (uint32_t)data;

  struct owl_toplevel *toplevel = server.focused_toplevel;

  enum owl_direction opposite_side;
  switch(direction) {
    case OWL_UP:
      opposite_side = OWL_DOWN;
      break;
    case OWL_DOWN:
      opposite_side = OWL_UP;
      break;
    case OWL_LEFT:
      opposite_side = OWL_RIGHT;
      break;
    case OWL_RIGHT:
      opposite_side = OWL_LEFT;
      break;
  }

  /* if no toplevel has keyboard focus then get the output
   * the pointer is on and try from there */
  if(toplevel == NULL) {
    struct wlr_output *wlr_output = wlr_output_layout_output_at(
      server.output_layout, server.cursor->x, server.cursor->y);
    struct owl_output *output = wlr_output->data;
    struct owl_output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
      focus_output(relative_output, opposite_side);
    }
    return;
  }

  /* get the toplevels output */
  struct owl_workspace *workspace = toplevel->workspace;
  struct owl_output *output = toplevel->workspace->output;
  struct owl_output *relative_output =
    output_get_relative(toplevel->workspace->output, direction);

  if(toplevel->fullscreen) {
    struct owl_output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
      focus_output(relative_output, opposite_side);
    }
    return;
  }

  if(toplevel->floating) {
    struct owl_toplevel *closest = toplevel_find_closest_floating_on_workspace(toplevel, direction);
    if(closest != NULL) {
      focus_toplevel(closest);
      cursor_jump_focused_toplevel();
      return;
    }
    struct owl_output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
      focus_output(relative_output, opposite_side);
    }
    return;
  }

  struct wl_list *next;
  if(toplevel_is_master(toplevel)) {
    switch(direction) {
      case OWL_RIGHT: {
        next = toplevel->link.next;
        if(next == &workspace->masters) {
          next = workspace->slaves.next;
          if(next == &workspace->slaves) {
            if(relative_output != NULL) {
              focus_output(relative_output, opposite_side);
            }
            return;
          }
        }
        struct owl_toplevel *t = wl_container_of(next, t, link);
        focus_toplevel(t);
        cursor_jump_focused_toplevel();
        return;
      }
      case OWL_LEFT: {
        next = toplevel->link.prev;
        if(next == &workspace->masters) {
          if(relative_output != NULL) {
            focus_output(relative_output, opposite_side);
          }
          return;
        }
        struct owl_toplevel *t = wl_container_of(next, t, link);
        focus_toplevel(t);
        cursor_jump_focused_toplevel();
        return;
      }
      default: {
        if(relative_output != NULL) {
          focus_output(relative_output, opposite_side);
        }
        return;
      }
    }
  }

  /* only case left is that the toplevel is a slave */
  switch(direction) {
    case OWL_LEFT: {
      struct owl_toplevel *last_master =
        wl_container_of(workspace->masters.prev, last_master, link);
      focus_toplevel(last_master);
      cursor_jump_focused_toplevel();
      return;
    }
    case OWL_RIGHT: {
      if(relative_output != NULL) {
        focus_output(relative_output, opposite_side);
      }
      return;
    }
    case OWL_UP: {
      struct wl_list *above = toplevel->link.prev;
      if(above == &workspace->slaves) {
        if(relative_output != NULL) {
          focus_output(relative_output, opposite_side);
        }
        return;
      }
      struct owl_toplevel *t = wl_container_of(above, t, link);
      focus_toplevel(t);
      cursor_jump_focused_toplevel();
      return;
    }
    case OWL_DOWN: {
      struct wl_list *bellow = toplevel->link.next;
      if(bellow == &workspace->slaves) {
        if(relative_output != NULL) {
          focus_output(relative_output, opposite_side);
        }
        return;
      }
      struct owl_toplevel *t = wl_container_of(bellow, t, link);
      focus_toplevel(t);
      cursor_jump_focused_toplevel();
      return;
    }
  }
}


static void
keybind_swap_focused_toplevel(void *data) {
  uint32_t direction = (uint32_t)data;

  struct owl_toplevel *toplevel = server.focused_toplevel;

  if(toplevel == NULL) return;

  struct owl_workspace *workspace = toplevel->workspace;
  struct owl_output *relative_output =
    output_get_relative(workspace->output, direction);

  if(toplevel->floating || toplevel->fullscreen) {
    if(relative_output != NULL
      && relative_output->active_workspace->fullscreen_toplevel == NULL) {
      toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
    }
    return;
  }

  struct wl_list *next;
  if(toplevel_is_master(toplevel)) {
    switch (direction) {
      case OWL_RIGHT: {
        next = toplevel->link.next;
        if(next == &workspace->masters) {
          next = workspace->slaves.next;
          if(next == &workspace->slaves) {
            if(relative_output != NULL
              && relative_output->active_workspace->fullscreen_toplevel == NULL) {
              toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
            }
            return;
          }
        }
        struct owl_toplevel *t = wl_container_of(next, t, link);
        layout_swap_tiled_toplevels(toplevel, t);
        cursor_jump_focused_toplevel();
        return;
      }
      case OWL_LEFT: {
        next = toplevel->link.prev;
        if(next == &workspace->masters) {
          if(relative_output != NULL
            && relative_output->active_workspace->fullscreen_toplevel == NULL) {
            toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
          }
          return;
        }
        struct owl_toplevel *t = wl_container_of(next, t, link);
        layout_swap_tiled_toplevels(t, toplevel);
        cursor_jump_focused_toplevel();
        return;
      }
      default: {
        struct owl_output *relative_output =
          output_get_relative(workspace->output, direction);
        if(relative_output != NULL
          && relative_output->active_workspace->fullscreen_toplevel == NULL) {
          toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
      }
    }
  }

  switch (direction) {
    case OWL_LEFT: {
      struct owl_toplevel *last_master =
        wl_container_of(workspace->masters.prev, last_master, link);
      layout_swap_tiled_toplevels(toplevel, last_master);
      cursor_jump_focused_toplevel();
      return;
    }
    case OWL_RIGHT: {
      struct owl_output *relative_output =
        output_get_relative(workspace->output, direction);
      if(relative_output != NULL
        && relative_output->active_workspace->fullscreen_toplevel == NULL) {
        toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
      }
      return;
    }
    case OWL_UP: {
      next = toplevel->link.prev;
      if(next == &workspace->slaves) {
        if(relative_output != NULL
          && relative_output->active_workspace->fullscreen_toplevel == NULL) {
          toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
      }
      struct owl_toplevel *t = wl_container_of(next, t, link);
      layout_swap_tiled_toplevels(t, toplevel);
      cursor_jump_focused_toplevel();
      return;
    }
    case OWL_DOWN: {
      next = toplevel->link.next;
      if(next == &workspace->slaves) {
        if(relative_output != NULL
          && relative_output->active_workspace->fullscreen_toplevel == NULL) {
          toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
      }
      struct owl_toplevel *t = wl_container_of(next, t, link);
      layout_swap_tiled_toplevels(toplevel, t);
      cursor_jump_focused_toplevel();
      return;
    }
  }
}

static void
keybind_switch_focused_toplevel_state(void *data) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL || toplevel->fullscreen) return;

  if(toplevel->floating) {
    toplevel->floating = false;
    wl_list_remove(&toplevel->link);

    if(wl_list_length(&toplevel->workspace->masters) < server.config->master_count) {
      wl_list_insert(toplevel->workspace->masters.prev, &toplevel->link);
    } else {
      wl_list_insert(toplevel->workspace->slaves.prev, &toplevel->link);
    }

    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    layout_send_configure(toplevel->workspace);
    return;
  }

  toplevel->floating = true;
  if(toplevel_is_master(toplevel)) {
    if(!wl_list_empty(&toplevel->workspace->slaves)) {
      struct owl_toplevel *s = wl_container_of(toplevel->workspace->slaves.next, s, link);
      wl_list_remove(&s->link);
      wl_list_insert(toplevel->workspace->masters.prev, &s->link);
    }
    wl_list_remove(&toplevel->link);
  } else {
    wl_list_remove(&toplevel->link);
  }

  wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);

  struct wlr_box output_box = toplevel->workspace->output->usable_area;
  uint32_t width, height;
  toplevel_floating_size(toplevel, &width, &height);
  toplevel_set_pending_state(toplevel, output_box.x + (output_box.width - WIDTH(toplevel)) / 2,
                             output_box.y + (output_box.height - HEIGHT(toplevel)) / 2,
                             WIDTH(toplevel), HEIGHT(toplevel));
  wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  layout_send_configure(toplevel->workspace);
}

static bool
config_add_window_rule(struct owl_config *c, char *app_id_regex, char *title_regex,
                       char *predicate, char **args, size_t arg_count) {
  struct window_rule_regex condition;
  if(strcmp(app_id_regex, "_") == 0) {
    condition.has_app_id_regex = false;
  } else {
    regex_t compiled;
    if(regcomp(&compiled, app_id_regex, REG_EXTENDED) != 0) {
      wlr_log(WLR_ERROR, "%s is not a valid regex", app_id_regex);
      regfree(&compiled);
      return false;
    }
    condition.app_id_regex = compiled;
    condition.has_app_id_regex = true;
  }

  if(strcmp(title_regex, "_") == 0) {
    condition.has_title_regex = false;
  } else {
    regex_t compiled;
    if(regcomp(&compiled, title_regex, REG_EXTENDED) != 0) {
      wlr_log(WLR_ERROR, "%s is not a valid regex", title_regex);
      regfree(&compiled);
      return false;
    }
    condition.title_regex = compiled;
    condition.has_title_regex = true;
  }

  if(strcmp(predicate, "float") == 0) {
    struct window_rule_float *window_rule = calloc(1, sizeof(*window_rule));
    window_rule->condition = condition;
    wl_list_insert(&c->window_rules.floating, &window_rule->link);
  } else if(strcmp(predicate, "size") == 0) {
    if(arg_count < 2) {
      wlr_log(WLR_ERROR, "invalid args to window_rule %s", predicate);
      return false;
    }
    struct window_rule_size *window_rule = calloc(1, sizeof(*window_rule));
    window_rule->condition = condition;

    /* if it ends with '%' we treat it as a relative unit */
    if(args[0][strlen(args[0]) - 1] == '%') {
      args[0][strlen(args[0]) - 1] = 0;
      window_rule->relative_width = true;
    }
    if(args[1][strlen(args[1]) - 1] == '%') {
      args[1][strlen(args[1]) - 1] = 0;
      window_rule->relative_height = true;
    }

    window_rule->width = atoi(args[0]);
    window_rule->height = atoi(args[1]);

    wl_list_insert(&c->window_rules.size, &window_rule->link);
  }

  return true;
}

static bool
config_add_keybind(struct owl_config *c, char *modifiers, char *key,
                   char* action, char **args, size_t arg_count) {
  char *p = modifiers;
  uint32_t modifiers_flag = 0;

  while(*p != '\0') {
    char mod[64] = {0};
    char *q = mod;
    while(*p != '+' && *p != '\0') {
      *q = *p;
      p++;
      q++;
    }

    if(strcmp(mod, "alt") == 0) {
      modifiers_flag |= WLR_MODIFIER_ALT;
    } else if(strcmp(mod, "super") == 0) {
      modifiers_flag |= WLR_MODIFIER_LOGO;
    } else if(strcmp(mod, "ctrl") == 0) {
      modifiers_flag |= WLR_MODIFIER_CTRL;
    } else if(strcmp(mod, "shift") == 0) {
      modifiers_flag |= WLR_MODIFIER_SHIFT;
    }

    if(*p == '+') {
      p++;
    }
  }

  uint32_t key_sym = 0;
  if(strcmp(key, "return") == 0 || strcmp(key, "enter") == 0) {
    key_sym = XKB_KEY_Return;
  } else if(strcmp(key, "backspace") == 0) {
    key_sym = XKB_KEY_BackSpace;
  } else if(strcmp(key, "delete") == 0) {
    key_sym = XKB_KEY_Delete;
  } else if(strcmp(key, "escape") == 0) {
    key_sym = XKB_KEY_Escape;
  } else if(strcmp(key, "tab") == 0) {
    key_sym = XKB_KEY_Tab;
  } else {
    key_sym = xkb_keysym_from_name(key, 0);
    if(key_sym == 0) {
      wlr_log(WLR_ERROR, "key %s doesn't seem right", key);
      return false;
    }
  }

  struct keybind *k = calloc(1, sizeof(*k));
  *k = (struct keybind){
    .modifiers = modifiers_flag,
    .sym = key_sym,
  };

  if(strcmp(action, "exit") == 0) {
    k->action = keybind_stop_server;
  } else if(strcmp(action, "run") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = keybind_run;
    char *args_0_copy = strdup(args[0]);
    k->args = args_0_copy;
  } else if(strcmp(action, "kill_active") == 0) {
    k->action = keybind_close_keyboard_focused_toplevel;
  } else if(strcmp(action, "switch_floating_state") == 0) {
    k->action = keybind_switch_focused_toplevel_state;
  } else if(strcmp(action, "resize") == 0) {
    k->action = keybind_resize_focused_toplevel;
    k->stop = keybind_stop_resize_focused_toplevel;
  } else if(strcmp(action, "move") == 0) {
    k->action = keybind_move_focused_toplevel;
    k->stop = keybind_stop_move_focused_toplevel;
  } else if(strcmp(action, "move_focus") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    enum owl_direction direction;
    if(strcmp(args[0], "up") == 0) {
      direction = OWL_UP;
    } else if(strcmp(args[0], "left") == 0) {
      direction = OWL_LEFT;
    } else if(strcmp(args[0], "down") == 0) {
      direction = OWL_DOWN;
    } else if(strcmp(args[0], "right") == 0) {
      direction = OWL_RIGHT;
    } else {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = keybind_move_focus;
    k->args = (void*)direction;
  } else if(strcmp(action, "swap") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    enum owl_direction direction;
    if(strcmp(args[0], "up") == 0) {
      direction = OWL_UP;
    } else if(strcmp(args[0], "left") == 0) {
      direction = OWL_LEFT;
    } else if(strcmp(args[0], "down") == 0) {
      direction = OWL_DOWN;
    } else if(strcmp(args[0], "right") == 0) {
      direction = OWL_RIGHT;
    } else {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = keybind_swap_focused_toplevel;
    k->args = (void*)direction;
  } else if(strcmp(action, "workspace") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }
    k->action = keybind_change_workspace;
    /* this is going to be overriden by the actual workspace that is needed for change_workspace() */
    k->args = (void*)atoi(args[0]);
  } else if(strcmp(action, "move_to_workspace") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }
    k->action = keybind_move_focused_toplevel_to_workspace;
    /* this is going to be overriden by the actual workspace that is needed for change_workspace() */
    k->args = (void*)atoi(args[0]);
  } else {
    wlr_log(WLR_ERROR, "invalid keybind action %s", action);
    free(k);
    return false;
  }

  wl_list_insert(&c->keybinds, &k->link);
  return true;
}

static void
config_free_args(char **args, size_t arg_count) {
  for(size_t i = 0; i < arg_count; i++) {
    if(args[i] != NULL) free(args[i]);
  }
}

static bool
config_handle_value(struct owl_config *c, char *keyword, char **args, size_t arg_count) {
  if(strcmp(keyword, "min_toplevel_size") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->min_toplevel_size = atoi(args[0]);
  } else if(strcmp(keyword, "keyboard_rate") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->keyboard_rate = atoi(args[0]);
  } else if(strcmp(keyword, "keyboard_delay") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->keyboard_delay = atoi(args[0]);
  } else if(strcmp(keyword, "natural_scroll") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->natural_scroll = atoi(args[0]);
  } else if(strcmp(keyword, "tap_to_click") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->tap_to_click = atoi(args[0]);
  } else if(strcmp(keyword, "border_width") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->border_width = atoi(args[0]);
  } else if(strcmp(keyword, "outer_gaps") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->outer_gaps = atoi(args[0]);
  } else if(strcmp(keyword, "inner_gaps") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->inner_gaps = atoi(args[0]);
  } else if(strcmp(keyword, "master_ratio") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->master_ratio = atof(args[0]);
  } else if(strcmp(keyword, "master_count") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->master_count = atoi(args[0]);
  } else if(strcmp(keyword, "cursor_theme") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->cursor_theme = strdup(args[0]);
  } else if(strcmp(keyword, "cursor_size") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->cursor_size = atoi(args[0]);
  } else if(strcmp(keyword, "inactive_border_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->inactive_border_color[0] = atoi(args[0]) / 256.0;
    c->inactive_border_color[1] = atoi(args[1]) / 256.0;
    c->inactive_border_color[2] = atoi(args[2]) / 256.0;
    c->inactive_border_color[3] = atoi(args[3]) / 256.0;
  } else if(strcmp(keyword, "active_border_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->active_border_color[0] = atoi(args[0]) / 256.0;
    c->active_border_color[1] = atoi(args[1]) / 256.0;
    c->active_border_color[2] = atoi(args[2]) / 256.0;
    c->active_border_color[3] = atoi(args[3]) / 256.0;
  } else if(strcmp(keyword, "output") == 0) {
    if(arg_count < 6) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    struct output_config *m = calloc(1, sizeof(*m));
    *m = (struct output_config){
      .name = strdup(args[0]),
      .x = atoi(args[1]),
      .y = atoi(args[2]),
      .width = atoi(args[3]),
      .height = atoi(args[4]),
      .refresh_rate = atoi(args[5]) * 1000,
    };
    wl_list_insert(&c->outputs, &m->link);
  } else if(strcmp(keyword, "workspace") == 0) {
    if(arg_count < 2) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    struct workspace_config *w = calloc(1, sizeof(*w));
    *w = (struct workspace_config){
      .index = atoi(args[0]),
      .output = strdup(args[1]),
    };
    wl_list_insert(&c->workspaces, &w->link);
  } else if(strcmp(keyword, "run") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    if(c->run_count >= 64) {
      wlr_log(WLR_ERROR, "do you really need 64 runs?");
      return false;
    }
    c->run[c->run_count] = strdup(args[0]);
    c->run_count++;
  } else if(strcmp(keyword, "keybind") == 0) {
    if(arg_count < 3) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    config_add_keybind(c, args[0], args[1], args[2], &args[3], arg_count - 3);
  } else if(strcmp(keyword, "env") == 0) {
    if(arg_count < 2) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    setenv(args[0], args[1], true);
  } else if(strcmp(keyword, "window_rule") == 0) {
    /* window_rule *regex* *predicate* *additional_args*
     * predicates:*
     *   float(no args),*
     *   size(two args: width, height) */
    if(arg_count < 3) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    config_add_window_rule(c, args[0], args[1], args[2], &args[3], arg_count - 3);
  } else if(strcmp(keyword, "animations") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->animations = atoi(args[0]);
  } else if(strcmp(keyword, "animation_duration") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->animation_duration = atoi(args[0]);
  } else if(strcmp(keyword, "animation_curve") == 0) {
    if(arg_count < 3) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->animation_curve[0] = atof(args[0]);
    c->animation_curve[1] = atof(args[1]);
    c->animation_curve[2] = atof(args[2]);
  } else {
    wlr_log(WLR_ERROR, "invalid keyword %s", keyword);
    config_free_args(args, arg_count);
    return false;
  }

  config_free_args(args, arg_count);
  return true;
}

static FILE *
try_open_config_file() {
  char path[512];
  char *config_home = getenv("XDG_CONFIG_HOME");
  if(config_home != NULL) {
    snprintf(path, sizeof(path), "%s/owl/owl.conf", config_home);
  } else {
    char *home = getenv("HOME");
    if(home != NULL) {
      snprintf(path, sizeof(path), "%s/.config/owl/owl.conf", home);
    } else {
      return NULL;
    }
  }

  return fopen(path, "r");
}

/* assumes the line is newline teriminated, as it should be with fgets() */
static bool
config_handle_line(char *line, size_t line_number, char **keyword,
                   char ***args, size_t *args_count) {
  char *p = line;

  /* skip whitespace */
  while(*p == ' ') p++;

  /* if its an empty line or it starts with '#' (comment) skip */
  if(*p == '\n' || *p == '#') {
    return false; 
  }

  size_t len = 0, cap = STRING_INITIAL_LENGTH;
  char *kw = calloc(cap, sizeof(char));
  size_t ars_len = 0, ars_cap = 8;
  char **ars = calloc(ars_cap, sizeof(*args));

  char *q = kw;
  while(*p != ' ') {
    if(len >= cap) {
      cap *= 2;
      keyword = realloc(keyword, cap);
      q = &kw[len];
    }
    *q = *p;
    p++;
    q++;
    len++;
  }
  *q = 0;

  /* skip whitespace */
  while(*p == ' ') p++;

  if(*p == '\n') {
    wlr_log(WLR_ERROR, "config: line %zu: no args provided for %s", line_number, kw);
    return false;
  }

  while(*p != '\n') {
    if(ars_len >= ars_cap) {
      ars_cap *= 2;
      ars = realloc(ars, ars_cap * sizeof(*ars));
    }

    len = 0;
    cap = STRING_INITIAL_LENGTH;
    ars[ars_len] = calloc(cap, sizeof(char));

    q = ars[ars_len];
    bool word = false;
    if(*p == '\"') {
      word = true;
      p++;
    };

    while((word && *p != '\"' && *p != '\n') || (!word && *p != ' ' && *p != '\n')) {
      if(len >= cap) {
        cap *= 2;
        ars[ars_len] = realloc(ars[ars_len], cap);
        q = &ars[ars_len][len];
      }
      *q = *p;
      p++;
      q++;
      len++;
    }
    *q = 0;
    ars_len++;

    if(word) p++;
    /* skip whitespace */
    while(*p == ' ') p++;
  }

  *args_count = ars_len;
  *keyword = kw;
  *args = ars;
  return true;
}

static bool
server_load_config() {
  struct owl_config *c = calloc(1, sizeof(*c));

  FILE *config_file = try_open_config_file();
  if(config_file == NULL) {
    wlr_log(WLR_INFO, "couldn't open config file, backing to default config");
    config_file = fopen("/usr/share/owl/default.conf", "r");
    if(config_file == NULL) {
      wlr_log(WLR_ERROR, "couldn't find the default config file");
      return false;
    } else {
      wlr_log(WLR_INFO, "using default config");
    }
  } else {
    wlr_log(WLR_INFO, "using custom config");
  }

  wl_list_init(&c->keybinds);
  wl_list_init(&c->outputs);
  wl_list_init(&c->workspaces);
  wl_list_init(&c->window_rules.floating);
  wl_list_init(&c->window_rules.size);

  /* you aint gonna have lines longer than 1kB */
  char line_buffer[1024] = {0};
  char *keyword, **args;
  size_t args_count;
  size_t line_number = 1;
  while(fgets(line_buffer, 1024, config_file) != NULL) {
    bool valid =
      config_handle_line(line_buffer, line_number, &keyword, &args, &args_count);
    if(valid) {
      config_handle_value(c, keyword, args, args_count);
    }
    line_number++;
  }

  fclose(config_file);

  /* as we are initializing config with calloc, some fields that are necessary in order
   * for owl to not crash may be not specified in the config.
   * we set their values to some default value.*/
  if(c->keyboard_rate == 0) {
    c->keyboard_rate = 150;
    wlr_log(WLR_INFO,
            "keyboard_rate not specified. using default %d", c->keyboard_rate);
  } 
  if(c->keyboard_delay == 0) {
    c->keyboard_delay = 50;
    wlr_log(WLR_INFO,
            "keyboard_delay not specified. using default %d", c->keyboard_delay);
  }
  if(c->master_count == 0) {
    c->master_count = 1;
    wlr_log(WLR_INFO,
            "master_count not specified. using default %lf", c->master_ratio);
  }
  if(c->master_ratio == 0) {
    /* here we evenly space toplevels if there is no master_ratio specified */
    c->master_ratio = c->master_count / (double)(c->master_count + 1);
    wlr_log(WLR_INFO,
            "master_ratio not specified. using default %lf", c->master_ratio);
  }
  if(c->cursor_size == 0) {
    c->cursor_size = 24;
    wlr_log(WLR_INFO,
            "cursor_size not specified. using default %d", c->cursor_size);
  }
  if(c->min_toplevel_size == 0) {
    c->min_toplevel_size = 10;
    wlr_log(WLR_INFO,
            "min_toplevel_size not specified. using default %d", c->min_toplevel_size);
  }

  server.config = c;
  return true;
}

int
main(int argc, char *argv[]) {
  /* this is ripped straight from chatgpt, it prevents the creation of zombie processes. */
  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  bool debug = false;
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--debug") == 0) {
      debug = true;
    }
  }

  if(debug) {
    /* make it so all the logs do to the log file */
    FILE *logs = fopen("/tmp/owl-logs", "w");
    if(logs != NULL) {
      int fd = fileno(logs);
      close(1);
      close(2);
      dup2(fd, 1);
      dup2(fd, 2);
      fclose(logs);
    }

    wlr_log_init(WLR_DEBUG, NULL);
  } else {
    wlr_log_init(WLR_INFO, NULL);
  }

  bool valid_config = server_load_config();
  if(!valid_config) {
    wlr_log(WLR_ERROR, "there is a problem in the config, quiting");
    return 1;
  }

  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, manging Wayland globals, and so on. */
  server.wl_display = wl_display_create();
  server.wl_event_loop = wl_display_get_event_loop(server.wl_display);

  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. */
  server.backend = wlr_backend_autocreate(server.wl_event_loop, NULL);
  if(server.backend == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_backend");
    return 1;
  }

  /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
   * can also specify a renderer using the WLR_RENDERER env var.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  server.renderer = wlr_renderer_autocreate(server.backend);
  if(server.renderer == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_renderer");
    return 1;
  }

  wlr_renderer_init_wl_display(server.renderer, server.wl_display);

  /* Autocreates an allocator for us.
   * The allocator is the bridge between the renderer and the backend. It
   * handles the buffer creation, allowing wlroots to render onto the
   * screen */
  server.allocator = wlr_allocator_autocreate(server.backend,
                                              server.renderer);
  if(server.allocator == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_allocator");
    return 1;
  }

  /* This creates some hands-off wlroots interfaces. The compositor is
   * necessary for clients to allocate surfaces, the subcompositor allows to
   * assign the role of subsurfaces to surfaces and the data device manager
   * handles the clipboard. Each of these wlroots interfaces has room for you
   * to dig your fingers in and play with their behavior if you want. Note that
   * the clients cannot set the selection directly without compositor approval,
   * see the handling of the request_set_selection event below.*/
  wlr_compositor_create(server.wl_display, 5, server.renderer);
  wlr_subcompositor_create(server.wl_display);
  wlr_data_device_manager_create(server.wl_display);

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  server.output_layout = wlr_output_layout_create(server.wl_display);

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&server.outputs);
  server.new_output.notify = server_handle_new_output;
  wl_signal_add(&server.backend->events.new_output, &server.new_output);

  /* create a manager used for comunicating with the clients */
  server.xdg_output_manager = wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);

  /* Create a scene graph. This is a wlroots abstraction that handles all
   * rendering and damage tracking. All the compositor author needs to do
   * is add things that should be rendered to the scene graph at the proper
   * positions and then call wlr_scene_output_commit() to render a frame if
   * necessary.
   */
  server.scene = wlr_scene_create();
  server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

  /* create all the scenes in the correct order */
  server.background_tree = wlr_scene_tree_create(&server.scene->tree);
  server.bottom_tree = wlr_scene_tree_create(&server.scene->tree);
  server.tiled_tree = wlr_scene_tree_create(&server.scene->tree);
  server.floating_tree = wlr_scene_tree_create(&server.scene->tree);
  server.top_tree = wlr_scene_tree_create(&server.scene->tree);
  server.fullscreen_tree = wlr_scene_tree_create(&server.scene->tree);
  server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);

  /* Set up xdg-shell version 6. The xdg-shell is a Wayland protocol which is
   * used for application windows. For more detail on shells, refer to
   * https://drewdevault.com/2018/07/29/Wayland-shells.html.
   */
  server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 6);
  server.new_xdg_toplevel.notify = server_handle_new_xdg_toplevel;
  wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
  server.new_xdg_popup.notify = server_handle_new_xdg_popup;
  wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

  server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 5);
  server.new_layer_surface.notify = server_handle_new_layer_surface;
  server.layer_shell->data = &server;
  wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

  /*
   * Creates a cursor, which is a wlroots utility for tracking the cursor
   * image shown on screen.
   */
  server.cursor = wlr_cursor_create();
  wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

  /* Creates an xcursor manager, another wlroots utility which loads up
   * Xcursor themes to source cursor images from and makes sure that cursor
   * images are available at all scale factors on the screen (necessary for
   * HiDPI support). */
  server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme, server.config->cursor_size);

  /*
   * wlr_cursor *only* displays an image on screen. It does not move around
   * when the pointer moves. However, we can attach input devices to it, and
   * it will generate aggregate events for all of them. In these events, we
   * can choose how we want to process them, forwarding them to clients and
   * moving the cursor around. More detail on this process is described in
   * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
   *
   * And more comments are sprinkled throughout the notify functions above.
   */

  server.cursor_mode = OWL_CURSOR_PASSTHROUGH;
  server.cursor_motion.notify = server_handle_cursor_motion;
  wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
  server.cursor_motion_absolute.notify = server_handle_cursor_motion_absolute;
  wl_signal_add(&server.cursor->events.motion_absolute,
                &server.cursor_motion_absolute);
  server.cursor_button.notify = server_handle_cursor_button;
  wl_signal_add(&server.cursor->events.button, &server.cursor_button);
  server.cursor_axis.notify = server_handle_cursor_axis;
  wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
  server.cursor_frame.notify = server_handle_cursor_frame;
  wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&server.keyboards);
  server.new_input.notify = server_handle_new_input;
  wl_signal_add(&server.backend->events.new_input, &server.new_input);
  server.seat = wlr_seat_create(server.wl_display, "seat0");
  server.request_cursor.notify = server_handle_request_cursor;
  wl_signal_add(&server.seat->events.request_set_cursor,
                &server.request_cursor);
  server.request_set_selection.notify = server_handle_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection,
                &server.request_set_selection);

  /* handles clipboard clients */
  server.data_control_manager = wlr_data_control_manager_v1_create(server.wl_display);

  /* configures decorations */
  server.xdg_decoration_manager =
    wlr_xdg_decoration_manager_v1_create(server.wl_display);

  server.request_xdg_decoration.notify = server_handle_request_xdg_decoration;
  wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
                &server.request_xdg_decoration);

  server.viewporter = wlr_viewporter_create(server.wl_display);

  server.screencopy_manager = wlr_screencopy_manager_v1_create(server.wl_display);
  server.foreign_toplevel_manager =
    wlr_foreign_toplevel_manager_v1_create(server.wl_display);

  /* Add a Unix socket to the Wayland display. */
  const char *socket = wl_display_add_socket_auto(server.wl_display);
  if (!socket) {
    wlr_backend_destroy(server.backend);
    return 1;
  }

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if(!wlr_backend_start(server.backend)) {
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 1;
  }

  /* Set the WAYLAND_DISPLAY environment variable to our socket */
  setenv("WAYLAND_DISPLAY", socket, true);

  /* creating a thread for the ipc to run on */
  pthread_t thread_id;
  pthread_create(&thread_id, NULL, run_ipc, NULL);

  /* sleep a bit so the ipc starts, 0.1 seconds is probably enough */
  usleep(100000);

  for(size_t i = 0; i < server.config->run_count; i++) {
    run_cmd(server.config->run[i]);
  }

  /* run the wayland event loop. */
  wlr_log(WLR_INFO, "running owl on WAYLAND_DISPLAY=%s", socket);
  wl_display_run(server.wl_display);

  /* Once wl_display_run returns, we destroy all clients then shut down the
   * server. */
  wl_display_destroy_clients(server.wl_display);
  wlr_scene_node_destroy(&server.scene->tree.node);
  wlr_xcursor_manager_destroy(server.cursor_mgr);
  wlr_cursor_destroy(server.cursor);
  wlr_allocator_destroy(server.allocator);
  wlr_renderer_destroy(server.renderer);
  wlr_backend_destroy(server.backend);
  wl_display_destroy(server.wl_display);
  return 0;
}
