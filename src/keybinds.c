#include "keybinds.h"

#include <scenefx/types/wlr_scene.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/xcursor.h>

#include "array.h"
#include "config.h"
#include "helpers.h"
#include "layout.h"
#include "mwc.h"
#include "pointer.h"
#include "toplevel.h"
#include "workspace.h"

extern struct server server;

void
keybind_stop_server(void *data) {
    server.mode = SERVER_MODE_SHUTTING;
    wl_display_terminate(server.wl_display);
}

void
keybind_run(void *data) {
    run_cmd(data);
}

void
keybind_change_workspace(void *data) {
    struct workspace *workspace = workspace_find_by_index((uintptr_t)data);

    if(workspace != NULL) {
        change_workspace(workspace, server.grabbed_toplevel != NULL);
    }
}

void
keybind_next_workspace(void *data) {
    struct workspace *current = server.active_workspace;
    struct wl_list *next = current->link.next;
    if(next == &current->output->workspaces) {
        next = current->output->workspaces.next;
    }
    struct workspace *next_workspace = wl_container_of(next, next_workspace, link);
    change_workspace(next_workspace, server.grabbed_toplevel != NULL);
}

void
keybind_prev_workspace(void *data) {
    struct workspace *current = server.active_workspace;
    struct wl_list *prev = current->link.prev;
    if(prev == &current->output->workspaces) {
        prev = current->output->workspaces.prev;
    }
    struct workspace *prev_workspace = wl_container_of(prev, prev_workspace, link);
    change_workspace(prev_workspace, server.grabbed_toplevel != NULL);
}

void
keybind_move_to_workspace(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel == server.grabbed_toplevel)
        return;

    struct workspace *workspace = workspace_find_by_index((uintptr_t)data);

    if(workspace != NULL) {
        toplevel_move_to_workspace(toplevel, workspace);
    }
}

static void
try_start_master_ratio_resize(void) {
    if(!has_slaves(server.active_workspace) ||
            !wlr_box_contains_point(&server.active_workspace->output->usable_area, server.cursor->x, server.cursor->y))
        return;

    server.grabbed_toplevel = NULL;
    server.mode = SERVER_MODE_RESIZING_MASTER_RATIO;

    server.grab_x = server.cursor->x;
    server.grab_y = server.cursor->y;

    server.initial_master_ratio = server.active_workspace->master_ratio;

    if(server.grab_x <= server.active_workspace->output->usable_area.x +
                    server.active_workspace->master_ratio * server.active_workspace->output->usable_area.width) {
        wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "right_side");
    } else {
        wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "left_side");
    }
}

void
keybind_start_resize(void *data) {
    if(server.mode != SERVER_MODE_NORMAL)
        return;

    struct toplevel *toplevel = get_toplevel_under_cursor();

    if(toplevel != NULL && toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        // if a floating toplevel is under the cursor then start the resize
        uint32_t edges = toplevel_get_closest_corner(server.cursor, toplevel);
        wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, wlr_xcursor_get_resize_name(edges));

        toplevel_start_resize(toplevel, edges, true);
    } else {
        // else we start the master ratio resize
        try_start_master_ratio_resize();
    }
}

void
keybind_stop_resize(void *data) {
    if(server.mode != SERVER_MODE_RESIZING && server.mode != SERVER_MODE_RESIZING_MASTER_RATIO)
        return;

    cursor_stop_move_resize();
}

void
keybind_start_move(void *data) {
    if(server.mode != SERVER_MODE_NORMAL)
        return;

    struct toplevel *toplevel = get_toplevel_under_cursor();
    if(toplevel == NULL || toplevel->mode == TOPLEVEL_MODE_FULLSCREEN)
        return;

    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "hand1");
    toplevel_start_move(toplevel, true);
}

void
keybind_stop_move(void *data) {
    if(server.mode != SERVER_MODE_MOVING)
        return;

    cursor_stop_move_resize();
}

void
keybind_close(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL)
        return;

    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void
try_focus_relative_output(struct output *output, enum direction direction) {
    struct output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
        focus_output(relative_output, direction);
    }
}

void
keybind_move_focus(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    // we need grabbed toplevel to keep focus
    if(server.grabbed_toplevel != NULL && toplevel == server.grabbed_toplevel)
        return;

    enum direction direction = (uintptr_t)data;

    // if no toplevel has keyboard focus then get the active output and try from there
    if(toplevel == NULL) {
        try_focus_relative_output(server.active_workspace->output, direction);
        return;
    }

    // get the toplevels output
    struct workspace *workspace = toplevel->workspace;
    struct output *output = toplevel->workspace->output;

    if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN) {
        // if fullscreen then just get the appropriate output to focus
        try_focus_relative_output(output, direction);
    } else if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        struct toplevel *closest = toplevel_find_closest_floating_on_workspace(toplevel, direction);
        if(closest != NULL) {
            focus_toplevel(closest, true);
        } else {
            try_focus_relative_output(output, direction);
        }
    } else if(toplevel->mode == TOPLEVEL_MODE_MASTER) {
        if(direction == DIRECTION_RIGHT) {
            struct toplevel *focus;
            if((focus = next_master(toplevel)) != NULL) {
                focus_toplevel(focus, true);
            } else if((focus = last_slave(workspace)) != NULL) {
                focus_toplevel(focus, true);
            } else {
                try_focus_relative_output(output, direction);
            }
        } else if(direction == DIRECTION_LEFT) {
            struct toplevel *focus;
            if((focus = prev_master(toplevel)) != NULL) {
                focus_toplevel(focus, true);
            } else {
                try_focus_relative_output(output, direction);
            }
        } else {
            try_focus_relative_output(output, direction);
        }
    } else {
        if(direction == DIRECTION_UP) {
            struct toplevel *focus;
            if((focus = prev_slave(toplevel)) != NULL) {
                focus_toplevel(focus, true);
            } else {
                try_focus_relative_output(output, direction);
            }
        } else if(direction == DIRECTION_DOWN) {
            struct toplevel *focus;
            if((focus = next_slave(toplevel)) != NULL) {
                focus_toplevel(focus, true);
            } else {
                try_focus_relative_output(output, direction);
            }
        } else if(direction == DIRECTION_LEFT) {
            focus_toplevel(last_master(workspace), true);
        } else {
            try_focus_relative_output(output, direction);
        }
    }
}

static void
try_move_relative_output(struct output *output, enum direction direction, struct toplevel *toplevel) {
    struct output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL && relative_output->active_workspace->fullscreen == NULL) {
        toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
    }
}
void
keybind_move(void *data) {
    uint64_t direction = (uint64_t)data;

    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel == server.grabbed_toplevel)
        return;

    struct workspace *workspace = toplevel->workspace;
    struct output *output = workspace->output;

    if(toplevel->mode == TOPLEVEL_MODE_FLOATING || toplevel->mode == TOPLEVEL_MODE_FULLSCREEN) {
        try_move_relative_output(output, direction, toplevel);
    } else if(toplevel->mode == TOPLEVEL_MODE_MASTER) {
        if(direction == DIRECTION_RIGHT) {
            struct toplevel *swap;
            if((swap = next_master(toplevel)) != NULL) {
                layout_swap(toplevel, swap);
            } else if((swap = last_slave(workspace)) != NULL) {
                layout_swap(toplevel, swap);
            } else {
                try_move_relative_output(output, direction, toplevel);
            }
        } else if(direction == DIRECTION_LEFT) {
            struct toplevel *swap;
            if((swap = prev_master(toplevel)) != NULL) {
                layout_swap(swap, toplevel);
            } else {
                try_move_relative_output(output, direction, toplevel);
            }
        } else {
            try_move_relative_output(output, direction, toplevel);
        }
    } else {
        if(direction == DIRECTION_UP) {
            struct toplevel *swap;
            if((swap = prev_slave(toplevel)) != NULL) {
                layout_swap(swap, toplevel);
            } else {
                try_move_relative_output(output, direction, toplevel);
            }
        } else if(direction == DIRECTION_DOWN) {
            struct toplevel *swap;
            if((swap = next_slave(toplevel)) != NULL) {
                layout_swap(toplevel, swap);
            } else {
                try_move_relative_output(output, direction, toplevel);
            }
        } else if(direction == DIRECTION_LEFT) {
            layout_swap(last_master(workspace), toplevel);
        } else {
            try_move_relative_output(output, direction, toplevel);
        }
    }
}

void
keybind_toggle_floating(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel->mode == TOPLEVEL_MODE_FULLSCREEN)
        return;

    struct workspace *workspace = toplevel->workspace;

    if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        wl_list_remove(&toplevel->link);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);

        layout_add(workspace, toplevel);
        layout_configure(workspace);
    } else {
        if(toplevel->mode == TOPLEVEL_MODE_MASTER && has_slaves(workspace)) {
            promote_last_slave(workspace);
        }
        wl_list_remove(&toplevel->link);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);

        toplevel->mode = TOPLEVEL_MODE_FLOATING;
        wl_list_insert(&toplevel->workspace->floating, &toplevel->link);

        uint32_t width, height;
        if(toplevel_get_floating_deco_size(toplevel, &width, &height)) {
            struct wlr_box centered = output_create_centered_box(workspace->output, width, height);
            toplevel_set_state(toplevel, centered);
        } else {
            toplevel_floating_set_own_size(toplevel);
        }

        toplevel_raise_to_top(toplevel);
        layout_configure(workspace);
    }
}

void
keybind_toggle_fullscreen(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel == server.grabbed_toplevel)
        return;

    if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN) {
        toplevel_unset_fullscreen(toplevel);
    } else {
        toplevel_set_fullscreen(toplevel);
    }
}

void
keybind_increase_master_ratio(void *data) {
    double delta = (uintptr_t)data / 100.0;
    struct workspace *workspace = server.active_workspace;

    workspace_set_master_ratio(workspace, workspace->master_ratio + delta);
}

void
keybind_decrease_master_ratio(void *data) {
    double delta = (uintptr_t)data / 100.0;
    struct workspace *workspace = server.active_workspace;

    workspace_set_master_ratio(workspace, workspace->master_ratio - delta);
}

bool
server_handle_keybinds(struct keyboard *keyboard, uint32_t keycode, enum wl_keyboard_key_state state) {
    if(server.lock != NULL)
        return false;

    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    // we use empty state so we can get raw, unmodified key.
    // this is used becuase we already handle modifiers explicitly,
    // and dont want them to interfere. for example, shift would make it
    // harder to specify the right key e.g. we would have to write
    //     keybind alt+shift # <do_something>
    // instead of
    //     alt+shift 3 <do_something> */

    const xkb_keysym_t *syms;
    int count = xkb_state_key_get_syms(keyboard->empty, keycode, &syms);

    for(size_t i = 0; i < count; i++) {
        for(struct keybind *iter = server.config->keybinds; iter <= array_last(server.config->keybinds); iter++) {
            if(iter->active && iter->stop && syms[i] == iter->key && state == WL_KEYBOARD_KEY_STATE_RELEASED) {
                iter->active = false;
                iter->stop(iter->data);
                return true;
            }

            if(modifiers == iter->modifiers && syms[i] == iter->key && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                iter->active = true;
                iter->action(iter->data);
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
        if(vt >= 1 && vt <= 12) {
            wlr_session_change_vt(server.session, vt);
            return true;
        }
    }
    return false;
}
