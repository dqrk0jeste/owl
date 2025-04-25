#include "keybinds.h"

#include <scenefx/types/wlr_scene.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/xcursor.h>

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
    server.running = false;
    wl_display_terminate(server.wl_display);
}

void
keybind_run(void *data) {
    run_cmd(data);
}

void
keybind_change_workspace(void *data) {
    struct workspace *workspace = data;
    change_workspace(workspace, server.grabbed_toplevel != NULL);
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
    if(toplevel == NULL || toplevel == server.grabbed_toplevel) return;

    struct workspace *workspace = data;
    toplevel_move_to_workspace(toplevel, workspace);
}

void
keybind_start_resize(void *data) {
    if(server.grabbed_toplevel != NULL) return;

    struct view *view = pointer_get_view_under_cursor();
    struct toplevel *toplevel = view == NULL ? NULL : view_try_get_toplevel(view);
    // if a floating toplevel is under the cursor then start the resize
    if(toplevel != NULL && toplevel->floating && !toplevel->fullscreen) {
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

        toplevel_start_resize(toplevel, edges, false);
        return;
    }

    // else we start the master ratio resize
    workspace_start_master_ratio_resize(toplevel->workspace);
}

void
keybind_stop_resize(void *data) {
    if(server.grabbed_toplevel == NULL) return;

    cursor_stop_move_resize();
}

void
keybind_start_move(void *data) {
    if(server.grabbed_toplevel != NULL) return;

    struct view *view = pointer_get_view_under_cursor();
    if(view == NULL) return;

    struct toplevel *toplevel = view_try_get_toplevel(view);
    if(toplevel == NULL || toplevel->fullscreen) return;

    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "hand1");
    toplevel_start_move(toplevel, false);
}

void
keybind_stop_move(void *data) {
    if(server.grabbed_toplevel == NULL) return;

    cursor_stop_move_resize();
}

void
keybind_close(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL) return;

    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

void
keybind_move_focus(void *data) {
    uintptr_t direction = (uintptr_t)data;

    struct toplevel *toplevel = server.focused_toplevel;
    // we need grabbed toplevel to keep focus
    if(server.grabbed_toplevel != NULL && toplevel == server.grabbed_toplevel) return;

    enum direction opposite_side;
    switch(direction) {
        case UP:
            opposite_side = DOWN;
            break;
        case DOWN:
            opposite_side = UP;
            break;
        case LEFT:
            opposite_side = RIGHT;
            break;
        case RIGHT:
            opposite_side = LEFT;
            break;
    }

    // if no toplevel has keyboard focus then get the output the pointer is on and try from there
    if(toplevel == NULL) {
        struct wlr_output *wlr_output =
                wlr_output_layout_output_at(server.output_layout, server.cursor->x, server.cursor->y);
        struct output *output = wlr_output->data;
        struct output *relative_output = output_get_relative(output, direction);
        if(relative_output != NULL) {
            focus_output(relative_output, opposite_side);
        }
        return;
    }

    // get the toplevels output
    struct workspace *workspace = toplevel->workspace;
    struct output *output = toplevel->workspace->output;
    struct output *relative_output = output_get_relative(toplevel->workspace->output, direction);

    if(toplevel->fullscreen) {
        struct output *relative_output = output_get_relative(output, direction);
        if(relative_output != NULL) {
            focus_output(relative_output, opposite_side);
        }
        return;
    }

    if(toplevel->floating) {
        struct toplevel *closest = toplevel_find_closest_floating_on_workspace(toplevel, direction);
        if(closest != NULL) {
            focus_toplevel(closest);
            cursor_jump_focused_toplevel();
            return;
        }
        struct output *relative_output = output_get_relative(output, direction);
        if(relative_output != NULL) {
            focus_output(relative_output, opposite_side);
        }
        return;
    }

    struct wl_list *next;
    if(toplevel_is_master(toplevel)) {
        switch(direction) {
            case RIGHT: {
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
                struct toplevel *t = wl_container_of(next, t, link);
                focus_toplevel(t);
                cursor_jump_focused_toplevel();
                return;
            }
            case LEFT: {
                next = toplevel->link.prev;
                if(next == &workspace->masters) {
                    if(relative_output != NULL) {
                        focus_output(relative_output, opposite_side);
                    }
                    return;
                }
                struct toplevel *t = wl_container_of(next, t, link);
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

    // only case left is that the toplevel is a slave
    switch(direction) {
        case LEFT: {
            struct toplevel *last_master = wl_container_of(workspace->masters.prev, last_master, link);
            focus_toplevel(last_master);
            cursor_jump_focused_toplevel();
            return;
        }
        case RIGHT: {
            if(relative_output != NULL) {
                focus_output(relative_output, opposite_side);
            }
            return;
        }
        case UP: {
            struct wl_list *above = toplevel->link.prev;
            if(above == &workspace->slaves) {
                if(relative_output != NULL) {
                    focus_output(relative_output, opposite_side);
                }
                return;
            }
            struct toplevel *t = wl_container_of(above, t, link);
            focus_toplevel(t);
            cursor_jump_focused_toplevel();
            return;
        }
        case DOWN: {
            struct wl_list *bellow = toplevel->link.next;
            if(bellow == &workspace->slaves) {
                if(relative_output != NULL) {
                    focus_output(relative_output, opposite_side);
                }
                return;
            }
            struct toplevel *t = wl_container_of(bellow, t, link);
            focus_toplevel(t);
            cursor_jump_focused_toplevel();
            return;
        }
    }
}

void
keybind_move(void *data) {
    uint64_t direction = (uint64_t)data;

    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel == server.grabbed_toplevel) return;

    struct workspace *workspace = toplevel->workspace;
    struct output *relative_output = output_get_relative(workspace->output, direction);

    if(toplevel->floating || toplevel->fullscreen) {
        if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
            toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
    }

    struct wl_list *next;
    if(toplevel_is_master(toplevel)) {
        switch(direction) {
            case RIGHT: {
                next = toplevel->link.next;
                if(next == &workspace->masters) {
                    next = workspace->slaves.prev;
                    if(next == &workspace->slaves) {
                        if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
                            toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
                        }
                        return;
                    }
                }
                struct toplevel *t = wl_container_of(next, t, link);
                layout_swap_toplevels(toplevel, t);
                return;
            }
            case LEFT: {
                next = toplevel->link.prev;
                if(next == &workspace->masters) {
                    if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
                        toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
                    }
                    return;
                }
                struct toplevel *t = wl_container_of(next, t, link);
                layout_swap_toplevels(t, toplevel);
                return;
            }
            default: {
                struct output *relative_output = output_get_relative(workspace->output, direction);
                if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
                    toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
                }
                return;
            }
        }
    }

    switch(direction) {
        case LEFT: {
            struct toplevel *last_master = wl_container_of(workspace->masters.prev, last_master, link);
            layout_swap_toplevels(toplevel, last_master);
            return;
        }
        case RIGHT: {
            struct output *relative_output = output_get_relative(workspace->output, direction);
            if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
                toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
            }
            return;
        }
        case UP: {
            next = toplevel->link.prev;
            if(next == &workspace->slaves) {
                if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
                    toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
                }
                return;
            }
            struct toplevel *t = wl_container_of(next, t, link);
            layout_swap_toplevels(t, toplevel);
            return;
        }
        case DOWN: {
            next = toplevel->link.next;
            if(next == &workspace->slaves) {
                if(relative_output != NULL && relative_output->active_workspace->fullscreen_toplevel == NULL) {
                    toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
                }
                return;
            }
            struct toplevel *t = wl_container_of(next, t, link);
            layout_swap_toplevels(toplevel, t);
            return;
        }
    }
}

void
keybind_toggle_floating(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel->fullscreen || toplevel == server.grabbed_toplevel) return;

    if(toplevel->floating) {
        toplevel->floating = false;
        wl_list_remove(&toplevel->link);

        if(wl_list_length(&toplevel->workspace->masters) < server.config->master_count) {
            wl_list_insert(toplevel->workspace->masters.prev, &toplevel->link);
        } else {
            wl_list_insert(toplevel->workspace->slaves.prev, &toplevel->link);
        }

        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);

        layout_configure(toplevel->workspace);
        return;
    }

    toplevel->floating = true;
    if(toplevel_is_master(toplevel)) {
        if(!wl_list_empty(&toplevel->workspace->slaves)) {
            struct toplevel *s = wl_container_of(toplevel->workspace->slaves.prev, s, link);
            wl_list_remove(&s->link);
            wl_list_insert(toplevel->workspace->masters.prev, &s->link);
        }
        wl_list_remove(&toplevel->link);
    } else {
        wl_list_remove(&toplevel->link);
    }

    wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);

    uint32_t width, height;
    if(toplevel_get_floating_deco_size(toplevel, &width, &height)) {
        struct wlr_box centered = output_create_centered_box(toplevel->workspace->output, width, height);
        toplevel_set_state(toplevel, centered);
    } else {
        toplevel_floating_set_own_size(toplevel);
    }

    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
    toplevel_raise_to_top(toplevel);

    layout_configure(toplevel->workspace);
}

void
keybind_toggle_fullscreen(void *data) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL || toplevel == server.grabbed_toplevel) return;

    if(toplevel->fullscreen) {
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
    if(server.lock != NULL) return false;

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

    struct keybind *k;
    for(size_t i = 0; i < count; i++) {
        wl_list_for_each(k, &server.config->keybinds, link) {
            if(!k->initialized) continue;

            if(k->active && k->stop && syms[i] == k->key && state == WL_KEYBOARD_KEY_STATE_RELEASED) {
                k->active = false;
                k->stop(k->args);
                return true;
            }

            if(modifiers == k->modifiers && syms[i] == k->key && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
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
        if(vt >= 1 && vt <= 12) {
            wlr_session_change_vt(server.session, vt);
            return true;
        }
    }
    return false;
}
