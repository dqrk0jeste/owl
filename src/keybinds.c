#include "keybinds.h"

#include "config.h"
#include "helpers.h"
#include "owl.h"
#include "toplevel.h"
#include "workspace.h"
#include "layout.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/xcursor.h>
#include <wlr/types/wlr_cursor.h>

extern struct owl_server server;

bool
server_handle_keybinds(struct owl_keyboard *keyboard, uint32_t keycode,
                       enum wl_keyboard_key_state state) {
  uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
  /* we create new empty state so we can get raw, unmodified key.
   * this is used becuase we already handle modifiers explicitly,
   * and dont want them to interfere. for example, shift would make it
   * harder to specify the right key e.g. we would have to write
   *   keybind alt+shift # <do_something>
   * instead of
   *   alt+shift 3 <do_something> */
  /* TODO: cache this */
  struct xkb_state *empty = xkb_state_new(keyboard->wlr_keyboard->keymap);

  const xkb_keysym_t *syms;
  int count = xkb_state_key_get_syms(empty, keycode, &syms);
  xkb_state_unref(empty);

  bool handled = handle_change_vt_key(syms, count);
  if(handled) return true;

  struct keybind *k;
  for(size_t i = 0; i < count; i++) {
    wl_list_for_each(k, &server.config->keybinds, link) {
      if(!k->initialized) continue;

      if(k->active && k->stop && syms[i] == k->sym
         && state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        k->active = false;
        k->stop(k->args);
        return true;
      }

      if(modifiers == k->modifiers && syms[i] == k->sym
         && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        k->active = true;
        k->action(k->args);
        return true;
      }
    }
  }

  return false;
}

bool
handle_change_vt_key(const xkb_keysym_t *keysyms, size_t count) {
	for(int i = 0; i < count; i++) {
	  uint32_t vt = keysyms[i] - XKB_KEY_XF86Switch_VT_1 + 1;
		if (vt >= 1 && vt <= 12) {
      wlr_session_change_vt(server.session, vt);
			return true;
		}
	}
	return false;
}

void
keybind_stop_server(void *data) {
  server.running = false;
  wl_display_terminate(server.wl_display);
}

void
keybind_run(void *data) {
  run_cmd(data);
}

void
keybind_change_workspace(void *data) {
  struct owl_workspace *workspace = data;
  change_workspace(workspace, false);
}

void
keybind_next_workspace(void *data) {
  struct owl_workspace *current = server.active_workspace;
  struct wl_list *next = current->link.next;
  if(next == &current->output->workspaces) {
    next = current->output->workspaces.next;
  }
  struct owl_workspace *next_workspace = wl_container_of(next, next_workspace, link);
  change_workspace(next_workspace, false);
}

void
keybind_prev_workspace(void *data) {
  struct owl_workspace *current = server.active_workspace;
  struct wl_list *prev = current->link.prev;
  if(prev == &current->output->workspaces) {
    prev = current->output->workspaces.prev;
  }
  struct owl_workspace *prev_workspace = wl_container_of(prev, prev_workspace, link);
  change_workspace(prev_workspace, false);
}

void
keybind_move_focused_toplevel_to_workspace(void *data) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  struct owl_workspace *workspace = data;
  toplevel_move_to_workspace(toplevel, workspace);
}

void
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
  toplevel_start_move_resize(toplevel, OWL_CURSOR_RESIZE, edges);
}

void
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

void
keybind_move_focused_toplevel(void *data) {
  struct owl_toplevel *toplevel = get_pointer_focused_toplevel();
  if(toplevel == NULL || !toplevel->floating || toplevel->fullscreen) return;

  wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "hand1");
  toplevel_start_move_resize(toplevel, OWL_CURSOR_MOVE, 0);
}

void
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

void
keybind_close_keyboard_focused_toplevel(void *data) {
  struct owl_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  xdg_toplevel_send_close(toplevel->xdg_toplevel->resource);
}

void
keybind_move_focus(void *data) {
  uint64_t direction = (uint64_t)data;

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
          next = workspace->slaves.prev;
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


void
keybind_swap_focused_toplevel(void *data) {
  uint64_t direction = (uint64_t)data;

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
    switch(direction) {
      case OWL_RIGHT: {
        next = toplevel->link.next;
        if(next == &workspace->masters) {
          next = workspace->slaves.prev;
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

  switch(direction) {
    case OWL_LEFT: {
      struct owl_toplevel *last_master =
        wl_container_of(workspace->masters.prev, last_master, link);
      layout_swap_tiled_toplevels(toplevel, last_master);
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
      return;
    }
  }
}

void
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

    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
    wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, WLR_EDGE_TOP & WLR_EDGE_RIGHT
                               & WLR_EDGE_BOTTOM & WLR_EDGE_LEFT);

    layout_set_pending_state(toplevel->workspace);
    return;
  }

  toplevel->floating = true;
  if(toplevel_is_master(toplevel)) {
    if(!wl_list_empty(&toplevel->workspace->slaves)) {
      struct owl_toplevel *s = wl_container_of(toplevel->workspace->slaves.prev, s, link);
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
  toplevel_set_pending_state(toplevel,
                             output_box.x + (output_box.width - width) / 2,
                             output_box.y + (output_box.height - height) / 2,
                             width, height);
  wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

  wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
  wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, 0);

  layout_set_pending_state(toplevel->workspace);
}

