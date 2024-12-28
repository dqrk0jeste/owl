#include "toplevel.h"

#include "config.h"
#include "ipc.h"
#include "layout.h"
#include "owl.h"
#include "rendering.h"
#include "something.h"
#include "wlr/util/edges.h"
#include "workspace.h"
#include "output.h"
#include "helpers.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/util/log.h>

extern struct owl_server server;

void
server_handle_new_toplevel(struct wl_listener *listener, void *data) {
  /* this event is raised when a client creates a new toplevel */
  struct wlr_xdg_toplevel *xdg_toplevel = data;
  /* allocate an owl_toplevel for this surface */
  struct owl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
  toplevel->xdg_toplevel = xdg_toplevel;

  /* listen to the various events it can emit */
  toplevel->map.notify = toplevel_handle_map;
  wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

  toplevel->unmap.notify = toplevel_handle_unmap;
  wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);

  toplevel->commit.notify = toplevel_handle_commit;
  wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

  toplevel->destroy.notify = toplevel_handle_destroy;
  wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

  toplevel->request_move.notify = toplevel_handle_request_move;
  wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);

  toplevel->request_resize.notify = toplevel_handle_request_resize;
  wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);

  toplevel->request_maximize.notify = toplevel_handle_request_maximize;
  wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);

  toplevel->request_fullscreen.notify = toplevel_handle_request_fullscreen;
  wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);

  toplevel->set_app_id.notify = toplevel_handle_set_app_id;
  wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);

  toplevel->set_title.notify = toplevel_handle_set_title;
  wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}

void
toplevel_handle_commit(struct wl_listener *listener, void *data) {
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
      layout_set_pending_state(toplevel->workspace);
    } else {
      wl_list_insert(toplevel->workspace->slaves.prev, &toplevel->link);
      layout_set_pending_state(toplevel->workspace);
    }

    if(!toplevel->floating && toplevel->workspace->fullscreen_toplevel != NULL) {
      /* layout_set_pending_state() does not send any configures if there is
       * a fullscreen toplevel on the workspace, but we need to send something
       * in order to respect the protocol. we dont really care,
       * as it is not going to be shown anyway*/
      toplevel_set_initial_state(toplevel, 0, 0, 0, 0);
    }

    return;
  }

  if(!toplevel->mapped) return;

  if(toplevel->resizing) {
    toplevel_commit(toplevel);
    return;
  }

  if(!toplevel->dirty || toplevel->xdg_toplevel->base->current.configure_serial
     < toplevel->configure_serial) return;

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
      toplevel->pending.width = toplevel_get_geometry(toplevel).width;
      toplevel->pending.height = toplevel_get_geometry(toplevel).height;
    }
  }

  toplevel_commit(toplevel);
}

void
toplevel_handle_map(struct wl_listener *listener, void *data) {
  /* called when the surface is mapped, or ready to display on-screen. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, map);

  toplevel->mapped = true;
  toplevel->dirty = false;

  /* add this toplevel to the scene tree */
  if(toplevel->floating) {
    toplevel->scene_tree = wlr_scene_xdg_surface_create(server.floating_tree,
                                                        toplevel->xdg_toplevel->base);
  } else {
    toplevel->scene_tree = wlr_scene_xdg_surface_create(server.tiled_tree,
                                                        toplevel->xdg_toplevel->base);
  }

  /* output at 0, 0 would get this toplevel flashed if its on some other output,
   * so we disable it until the next frame */
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);

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

  /* add foreign toplevel handler */
  toplevel->foreign_toplevel_handle =
    wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);

  wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel_handle,
                                           toplevel->xdg_toplevel->title);
  wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel_handle,
                                            toplevel->xdg_toplevel->app_id);

  focus_toplevel(toplevel);

  if(toplevel->floating) {
    /* we have sent the configure, but we dont care if it chose some other size */
    toplevel->pending.width = toplevel_get_geometry(toplevel).width;
    toplevel->pending.height = toplevel_get_geometry(toplevel).height;

    struct wlr_box output_box = toplevel->workspace->output->usable_area;
    toplevel->animation.initial.x = output_box.x + output_box.width / 2;
    toplevel->animation.initial.y = output_box.y + output_box.height / 2;
    toplevel_center_floating(toplevel);
  } 

  toplevel_commit(toplevel);
}

void
toplevel_handle_unmap(struct wl_listener *listener, void *data) {
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
        ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
      }
    }

    wl_list_remove(&toplevel->link);
    return;
  }

  if(toplevel_is_master(toplevel)) {
    /* we find a new master to replace him if possible */
    if(!wl_list_empty(&workspace->slaves)) {
      struct owl_toplevel *s = wl_container_of(workspace->slaves.prev, s, link);
      wl_list_remove(&s->link);
      wl_list_insert(workspace->masters.prev, &s->link);
    }
    if(toplevel == server.focused_toplevel) {
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
        ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
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

  layout_set_pending_state(toplevel->workspace);
}

void
toplevel_handle_destroy(struct wl_listener *listener, void *data) {
  /* called when the xdg_toplevel is destroyed. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

  wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel_handle);

  wl_list_remove(&toplevel->map.link);
  wl_list_remove(&toplevel->unmap.link);
  wl_list_remove(&toplevel->commit.link);
  wl_list_remove(&toplevel->destroy.link);
  wl_list_remove(&toplevel->request_move.link);
  wl_list_remove(&toplevel->request_resize.link);
  wl_list_remove(&toplevel->request_maximize.link);
  wl_list_remove(&toplevel->request_fullscreen.link);

  /* free out owl_something */
  free(toplevel->scene_tree->node.data);
  free(toplevel);
}

struct wlr_box
toplevel_get_geometry(struct owl_toplevel *toplevel) {
  struct wlr_box geometry;
  wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
  return geometry;
}

void
toplevel_start_move_resize(struct owl_toplevel *toplevel,
                           enum owl_cursor_mode mode, uint32_t edges) {
  if(toplevel == NULL) {
    return;
  }

  server.grabbed_toplevel = toplevel;
  server.cursor_mode = mode;

  server.grab_x = server.cursor->x;
  server.grab_y = server.cursor->y;

  struct wlr_box geo_box = toplevel_get_geometry(toplevel);
  server.grabbed_toplevel_initial_box = geo_box;
  server.grabbed_toplevel_initial_box.x += toplevel->scene_tree->node.x;
  server.grabbed_toplevel_initial_box.y += toplevel->scene_tree->node.y;

  server.resize_edges = edges;
}

void
toplevel_handle_request_move(struct wl_listener *listener, void *data) {
  /* This event is raised when a client would like to begin an interactive
   * move, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provided serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
  if(!toplevel->floating || toplevel != get_pointer_focused_toplevel()) return;

  toplevel_start_move_resize(toplevel, OWL_CURSOR_MOVE, 0);
}

void
toplevel_handle_request_resize(struct wl_listener *listener, void *data) {
  /* This event is raised when a client would like to begin an interactive
   * resize, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provided serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct wlr_xdg_toplevel_resize_event *event = data;

  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
  if(!toplevel->floating || toplevel != get_pointer_focused_toplevel()) return;

  toplevel_start_move_resize(toplevel, OWL_CURSOR_RESIZE, event->edges);
}

void
toplevel_handle_request_maximize(struct wl_listener *listener, void *data) {
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

void
toplevel_handle_request_fullscreen(struct wl_listener *listener, void *data) {
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel,
                                                  request_fullscreen);

  struct owl_output *output = toplevel->workspace->output;
  if(toplevel->xdg_toplevel->requested.fullscreen) {
    toplevel_set_fullscreen(toplevel);
  } else {
    toplevel_unset_fullscreen(toplevel);
  }
}

void
toplevel_handle_set_app_id(struct wl_listener *listener, void *data) {
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);

  if(toplevel->mapped) {
    wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel_handle,
                                              toplevel->xdg_toplevel->app_id);
  }

  if(toplevel == server.focused_toplevel) {
    ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
  }
}

void
toplevel_handle_set_title(struct wl_listener *listener, void *data) {
  struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);

  if(toplevel->mapped) {
    wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel_handle,
                                             toplevel->xdg_toplevel->title);
  }

  if(toplevel == server.focused_toplevel) {
    ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
  }
}

bool
toplevel_size_changed(struct owl_toplevel *toplevel) {
  return toplevel_get_geometry(toplevel).width != toplevel->pending.width
         || toplevel_get_geometry(toplevel).height != toplevel->pending.height;
}

bool
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

void
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

  *width = toplevel_get_geometry(toplevel).width;
  *height = toplevel_get_geometry(toplevel).height;
}

bool
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

/* TODO: return owl_something and check for layer_surfaces */
struct owl_toplevel *
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

/* TODO: here we should use clipped size for tiled toplevels */
void
cursor_jump_focused_toplevel(void) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  struct wlr_box geo_box = toplevel_get_geometry(toplevel);
  wlr_cursor_warp(server.cursor, NULL,
                  toplevel->scene_tree->node.x + geo_box.x + toplevel->current.width / 2.0,
                  toplevel->scene_tree->node.y + geo_box.y + toplevel->current.height / 2.0);
}

void
toplevel_center_floating(struct owl_toplevel *toplevel) {
  assert(toplevel->floating);

  struct wlr_box output_box = toplevel->workspace->output->usable_area;
  toplevel->pending.x = output_box.x + (output_box.width - toplevel->pending.width) / 2;
  toplevel->pending.y = output_box.y + (output_box.height - toplevel->pending.height) / 2;
}

void
toplevel_set_initial_state(struct owl_toplevel *toplevel, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
  assert(!toplevel->mapped);

  if(server.config->animations) {
    toplevel->animation.should_animate = true;
    toplevel->animation.initial = (struct wlr_box){
      .x = x + width / 2,
      .y = y + height / 2,
      .width = 1,
      .height = 1,
    };
  } else {
    toplevel->animation.should_animate = false;
  }

  toplevel->pending = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  toplevel->configure_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                                                         width, height);
  toplevel->dirty = true;
}

void
toplevel_set_pending_state(struct owl_toplevel *toplevel, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height) {
  assert(toplevel->mapped);

  struct wlr_box pending = {
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  toplevel->pending = pending;

  if(!server.config->animations || toplevel == server.grabbed_toplevel
     || wlr_box_equal(&toplevel->current, &pending)) {
    toplevel->animation.should_animate = false;
  } else {
    toplevel->animation.should_animate = true;
    toplevel->animation.initial = toplevel->current;
  }

  if((toplevel->floating || toplevel->fullscreen) && !toplevel_size_changed(toplevel)) {
    toplevel_commit(toplevel);
    return;
  };

  toplevel->configure_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                                                         width, height);
  toplevel->dirty = true;
}

void
toplevel_commit(struct owl_toplevel *toplevel) {
  toplevel->current = toplevel->pending;

  if(toplevel->animation.should_animate) {
    if(toplevel->animation.running) {
      /* if there is already an animation running, we start this one from the current state */
      toplevel->animation.initial = toplevel->animation.current;
    }
    toplevel->animation.passed_frames = 0;
    toplevel->animation.total_frames = server.config->animation_duration
      / output_frame_duration_ms(toplevel->workspace->output);

    toplevel->animation.running = true;
    toplevel->animation.should_animate = false;
  }

  wlr_output_schedule_frame(toplevel->workspace->output->wlr_output);
}

void
toplevel_set_fullscreen(struct owl_toplevel *toplevel) {
  wlr_log(WLR_ERROR, "");
  if(!toplevel->mapped) return;

  wlr_log(WLR_ERROR, "");
  if(toplevel->workspace->fullscreen_toplevel != NULL) return;

  wlr_log(WLR_ERROR, "");
  struct owl_workspace *workspace = toplevel->workspace;
  struct owl_output *output = workspace->output;

  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout,
                            output->wlr_output, &output_box);

  wlr_log(WLR_ERROR, "");
  toplevel->prev_geometry = toplevel->current;

  workspace->fullscreen_toplevel = toplevel;
  toplevel->fullscreen = true;

  wlr_log(WLR_ERROR, "");
  wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
  wlr_log(WLR_ERROR, "");
  toplevel_set_pending_state(toplevel, output_box.x, output_box.y,
                             output_box.width, output_box.height);
  wlr_log(WLR_ERROR, "");
  wlr_scene_node_reparent(&toplevel->scene_tree->node, server.fullscreen_tree);
  wlr_log(WLR_ERROR, "");

  wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel_handle, true);
  wlr_log(WLR_ERROR, "");
}

void
toplevel_unset_fullscreen(struct owl_toplevel *toplevel) {
  if(toplevel->workspace->fullscreen_toplevel != toplevel) return;

  wlr_log(WLR_ERROR, "");
  struct owl_workspace *workspace = toplevel->workspace;
  struct owl_output *output = workspace->output;

  wlr_log(WLR_ERROR, "");
  workspace->fullscreen_toplevel = NULL;
  toplevel->fullscreen = false;

  wlr_log(WLR_ERROR, "");
  wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);

  wlr_log(WLR_ERROR, "");
  if(toplevel->floating) {
  wlr_log(WLR_ERROR, "");
    toplevel_set_pending_state(toplevel,
                               toplevel->prev_geometry.x, toplevel->prev_geometry.y,
                               toplevel->prev_geometry.width, toplevel->prev_geometry.height);
  wlr_log(WLR_ERROR, "");
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
  } else {
  wlr_log(WLR_ERROR, "");
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
  }

  wlr_log(WLR_ERROR, "");
  layout_set_pending_state(workspace);
  
  wlr_log(WLR_ERROR, "");
  wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel_handle, false);
  wlr_log(WLR_ERROR, "");
}

void
toplevel_move(void) {
  /* move the grabbed toplevel to the new position */
  struct owl_toplevel *toplevel = server.grabbed_toplevel;
  struct wlr_box geometry = toplevel_get_geometry(toplevel);

  int new_x = server.grabbed_toplevel_initial_box.x + (server.cursor->x - server.grab_x);
  int new_y = server.grabbed_toplevel_initial_box.y + (server.cursor->y - server.grab_y);

  toplevel_set_pending_state(toplevel, new_x - geometry.x, new_y - geometry.y,
                             geometry.width, geometry.height);
}

void
toplevel_resize(void) {
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

  struct wlr_box geometry = toplevel_get_geometry(toplevel);
  toplevel_set_pending_state(toplevel, new_x - geometry.x, new_y - geometry.y,
                             new_width, new_height);
}

void
unfocus_focused_toplevel(void) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  server.focused_toplevel = NULL;
  /* deactivate the surface */
  wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
  /* clear all focus on the keyboard, focusing new should set new toplevel focus */
  wlr_seat_keyboard_clear_focus(server.seat);

  ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
  wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle, false);

  /* we schedule a frame in order for borders to be redrawn */
  wlr_output_schedule_frame(toplevel->workspace->output->wlr_output);
}

void
focus_toplevel(struct owl_toplevel *toplevel) {
  assert(toplevel != NULL);

  if(server.layer_exclusive_keyboard != NULL) return;

  if(toplevel->workspace->fullscreen_toplevel != NULL
    && toplevel != toplevel->workspace->fullscreen_toplevel) return;

  struct owl_toplevel *prev_toplevel = server.focused_toplevel;
  if(prev_toplevel == toplevel) return;

  if(prev_toplevel != NULL) {
    wlr_xdg_toplevel_set_activated(prev_toplevel->xdg_toplevel, false);
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle, false);
  }

  server.focused_toplevel = toplevel;

  if(toplevel->floating) {
    wl_list_remove(&toplevel->link);
    wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
  }

	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

  struct wlr_seat *seat = server.seat;
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  if(keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(seat, toplevel->xdg_toplevel->base->surface,
                                   keyboard->keycodes, keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }

  ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
  wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle, true);

  /* we schedule a frame in order for borders to be redrawn */
  wlr_output_schedule_frame(toplevel->workspace->output->wlr_output);
}


struct owl_toplevel *
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
        if(t == toplevel || X(t) > Y(toplevel)) continue;

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

struct owl_output *
toplevel_get_primary_output(struct owl_toplevel *toplevel) {
  struct wlr_box intersection_box;
  struct wlr_box output_box;
  uint32_t max_area = 0;
  struct owl_output *max_area_output = NULL;

  struct owl_output *o;
  wl_list_for_each(o, &server.outputs, link) {
    wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);
    bool intersects =
      wlr_box_intersection(&intersection_box, &toplevel->current, &output_box);
    if(intersects && box_area(&intersection_box) > max_area) {
      max_area = box_area(&intersection_box);
      max_area_output = o;
    }
  }

  return max_area_output;
}

void
toplevel_clip_to_size(struct owl_toplevel *toplevel,
                      uint32_t width, uint32_t height) {
  struct wlr_box clip_box = (struct wlr_box){
    .x = toplevel_get_geometry(toplevel).x,
    .y = toplevel_get_geometry(toplevel).y,
    .width = width,
    .height = height,
  };

  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);

  struct wlr_scene_node *n;
  wl_list_for_each(n, &toplevel->scene_tree->children, link) {
    struct owl_something *view = n->data;
    if(view != NULL && view->type == OWL_POPUP) {
      wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, NULL);
    }
  }
}

void
toplevel_clip_to_fit(struct owl_toplevel *toplevel) {
  /* we only clip tiled toplevels if they are too big to fit the layout */
  assert(!toplevel->floating);

  struct wlr_box clip_box = (struct wlr_box){
    .x = toplevel_get_geometry(toplevel).x,
    .y = toplevel_get_geometry(toplevel).y,
    .width = toplevel->current.width,
    .height = toplevel->current.height,
  };

  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);
  struct wlr_scene_node *n;
  wl_list_for_each(n, &toplevel->scene_tree->children, link) {
    struct owl_something *thing = n->data;
    if(thing != NULL && thing->type == OWL_POPUP) {
      wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, NULL);
    }
  }
}

void
toplevel_unclip_size(struct owl_toplevel *toplevel) {
  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, NULL);
}

uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor,
                            struct owl_toplevel *toplevel) {
  struct wlr_box geometry = toplevel_get_geometry(toplevel);

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

