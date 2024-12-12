#include "output.h"

#include "owl.h"
#include "config.h"
#include "keybinds.h"
#include "layout.h"
#include "rendering.h"
#include "workspace.h"
#include "toplevel.h"
#include "ipc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_cursor.h>

extern struct owl_server server;

void
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

double
output_frame_duration_ms(struct owl_output *output) {
  return 1000000.0 / output->wlr_output->refresh;
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

void
cursor_jump_output(struct owl_output *output) {
  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  wlr_cursor_warp(server.cursor, NULL,
                  output_box.x + output_box.width / 2.0,
                  output_box.y + output_box.height / 2.0);
}

void
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
    focus_next = layout_find_closest_tiled_toplevel(output->active_workspace,
                                                       master, side);
    /* if there are no tiled toplevels we try floating */
    if(focus_next == NULL) {
      focus_next = layout_find_closest_floating_toplevel(output->active_workspace,
                                                            side);
    }
  } else {
    focus_next = layout_find_closest_floating_toplevel(output->active_workspace,
                                                          side);
    /* if there are no floating toplevels we try tiled */
    if(focus_next == NULL) {
      focus_next = layout_find_closest_tiled_toplevel(output->active_workspace,
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

void
output_handle_frame(struct wl_listener *listener, void *data) {
  /* this function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate */
  struct owl_output *output = wl_container_of(listener, output, frame);
  struct owl_workspace *workspace = output->active_workspace;

  bool animations_done = true;
  struct owl_toplevel *t;
  if(!layout_is_ready(workspace)) {
    layout_render_dirty(workspace);
  } else {
    workspace_render_frame(workspace);
  }

  workspace_handle_opacity(workspace);

  struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server.scene,
                                                                     output->wlr_output);
  wlr_scene_output_commit(scene_output, NULL);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  wlr_scene_output_send_frame_done(scene_output, &now);
}

void
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
void
output_handle_destroy(struct wl_listener *listener, void *data) {
  struct owl_output *output = wl_container_of(listener, output, destroy);

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->link);
  free(output);
}

