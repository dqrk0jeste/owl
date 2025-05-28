#include "cursor.h"

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "array.h"
#include "constraint.h"
#include "ipc.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "rules.h"
#include "seat.h"

extern struct server server;

static void
handle_button(struct wl_listener *listener, void *data) {
    struct wlr_pointer_button_event *event = data;

    // drop the toplevel if grabbed
    if(event->button == 272 && event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
            (server.mode == SERVER_MODE_MOVING || server.mode == SERVER_MODE_RESIZING) &&
            !server.move_resize_by_keybind) {
        cursor_stop_move_resize();
        return;
    }

    // we get currently active modifiers and lookup pointer keybinds
    uint32_t modifiers =
            server.last_used_keyboard != NULL ? wlr_keyboard_get_modifiers(server.last_used_keyboard->wlr_keyboard) : 0;

    for(struct keybind *iter = server.config->pointer_keybinds; iter <= array_last(server.config->pointer_keybinds);
            iter++) {
        if(iter->active && iter->stop && event->button == iter->key &&
                event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
            iter->active = false;
            iter->stop(iter->data);
            return;
        }

        if(modifiers == iter->modifiers && event->button == iter->key &&
                event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
            iter->active = true;
            iter->action(iter->data);
            return;
        }
    }

    // if still not handled notify the client with pointer focus that a button press has occurred
    wlr_seat_pointer_notify_button(server.seat.base, event->time_msec, event->button, event->state);

    struct wlr_surface *surface;
    double sx, sy;
    struct view *view = view_at(server.cursor.base->x, server.cursor.base->y, &surface, &sx, &sy);

    if(view == NULL)
        return;

    if(view->type == VIEW_TITLEBAR_CLOSE_BUTTON && event->button == 272 &&
            event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        struct toplevel *toplevel = view_try_get_toplevel(view);
        if(toplevel != NULL) {
            wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
        }
    } else if((view->type == VIEW_TITLEBAR_BASE || view->type == VIEW_TITLEBAR_TITLE) && event->button == 272 &&
            event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        struct toplevel *toplevel = view_try_get_toplevel(view);
        if(toplevel != NULL) {
            toplevel_start_move(toplevel, false);
        }
    }
}

static void
handle_axis(struct wl_listener *listener, void *data) {
    struct wlr_pointer_axis_event *event = data;

    // notify the client with pointer focus of the axis event
    wlr_seat_pointer_notify_axis(server.seat.base, event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source, event->relative_direction);
}

static void
handle_frame(struct wl_listener *listener, void *data) {
    wlr_seat_pointer_notify_frame(server.seat.base);
}

static void
grabbed_toplevel_move(void) {
    // move the grabbed toplevel to the new position
    struct toplevel *toplevel = server.grabbed_toplevel;

    struct wlr_box box = server.grabbed_toplevel_initial_box;
    box.x += server.cursor.base->x - server.grab_x;
    box.y += server.cursor.base->y - server.grab_y;

    toplevel_set_state(toplevel, box);
}

static void
grabbed_toplevel_resize(void) {
    struct toplevel *toplevel = server.grabbed_toplevel;

    struct wlr_box start_box = server.grabbed_toplevel_initial_box;
    struct wlr_box new_box = server.grabbed_toplevel_initial_box;

    int min_width = max(toplevel->xdg_toplevel->current.min_width, 10);
    int min_height = max(toplevel->xdg_toplevel->current.min_height, 10);
    decoration_get_decoration_size(&toplevel->decoration, &min_width, &min_height);

    min_width = max(min_width, toplevel->decoration.min_width);

    int max_width = toplevel->xdg_toplevel->current.max_width;
    // client might not report the max size, or it might report some really big number for it. since `wlr_box` uses
    // platfom dependent `int` for storing such values, it might overflow (so it is a negative value) or it might be set
    // to `INT_MAX` for example, so our calucalations overflow. we patch it to 10000 in either case then
    if(max_width <= 0 || max_width > 10000)
        max_width = 10000;

    int max_height = toplevel->xdg_toplevel->current.max_height;
    if(max_height <= 0 || max_height > 10000)
        max_height = 10000;

    decoration_get_decoration_size(&toplevel->decoration, &max_width, &max_height);

    int dx = server.cursor.base->x - server.grab_x;
    int dy = server.cursor.base->y - server.grab_y;

    if(server.resize_edges & WLR_EDGE_TOP) {
        new_box.y = start_box.y + dy;
        new_box.height = start_box.height - dy;
        if(new_box.height < min_height) {
            new_box.y = start_box.y + start_box.height - min_height;
            new_box.height = min_height;
        } else if(new_box.height > max_height) {
            new_box.y = start_box.y + start_box.height - max_height;
            new_box.height = max_height;
        }
    } else if(server.resize_edges & WLR_EDGE_BOTTOM) {
        new_box.y = start_box.y;
        new_box.height = start_box.height + dy;
        if(new_box.height < min_height) {
            new_box.height = min_height;
        } else if(new_box.height > max_height) {
            new_box.height = max_height;
        }
    }

    if(server.resize_edges & WLR_EDGE_LEFT) {
        new_box.x = start_box.x + dx;
        new_box.width = start_box.width - dx;
        if(new_box.width < min_width) {
            new_box.x = start_box.x + start_box.width - min_width;
            new_box.width = min_width;
        } else if(new_box.width > max_width) {
            new_box.x = start_box.x + start_box.width - max_width;
            new_box.width = max_width;
        }
    } else if(server.resize_edges & WLR_EDGE_RIGHT) {
        new_box.x = start_box.x;
        new_box.width = start_box.width + dx;
        if(new_box.width < min_width) {
            new_box.width = min_width;
        } else if(new_box.width > max_width) {
            new_box.width = max_width;
        }
    }

    toplevel_set_state(toplevel, new_box);
}

static void
master_ratio_resize(void) {
    double moved_relative =
            (double)(server.cursor.base->x - server.grab_x) / server.active_workspace->output->usable_area.width;

    double master_ratio = clamp(server.initial_master_ratio + moved_relative, 0.0, 1.0);
    workspace_set_master_ratio(server.active_workspace, master_ratio);
}

static void
handle_motion_shared(uint32_t time) {
    // get the output that the cursor is on currently
    struct output *output = cursor_get_output();
    if(output == NULL)
        return;

    // set global active workspace
    if(output->active_workspace != server.active_workspace) {
        server.active_workspace = output->active_workspace;
        ipc_send_active_workspace();
    }

    if(server.mode == SERVER_MODE_MOVING) {
        grabbed_toplevel_move();
    } else if(server.mode == SERVER_MODE_RESIZING) {
        grabbed_toplevel_resize();
    } else if(server.mode == SERVER_MODE_RESIZING_MASTER_RATIO) {
        master_ratio_resize();
    } else if(server.mode == SERVER_MODE_DRAGGING) {
        dnd_icons_move(server.cursor.base->x, server.cursor.base->y);
    } else {
        cursor_handle_focus(time, true);
    }
}

static void
handle_motion(struct wl_listener *listener, void *data) {
    struct wlr_pointer_motion_event *event = data;

    constraint_apply_to_move(&event->delta_x, &event->delta_y);

    wlr_relative_pointer_manager_v1_send_relative_motion(server.relative_pointer_manager.base, server.seat.base,
            (uint64_t)event->time_msec * 1000, event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);

    wlr_cursor_move(server.cursor.base, &event->pointer->base, event->delta_x, event->delta_y);
    handle_motion_shared(event->time_msec);
}

static void
handle_motion_absolute(struct wl_listener *listener, void *data) {
    struct wlr_pointer_motion_absolute_event *event = data;

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(server.cursor.base, &event->pointer->base, event->x, event->y, &lx, &ly);

    double dx = lx - server.cursor.base->x;
    double dy = ly - server.cursor.base->y;

    wlr_relative_pointer_manager_v1_send_relative_motion(server.relative_pointer_manager.base, server.seat.base,
            (uint64_t)event->time_msec * 1000, dx, dy, dx, dy);

    wlr_cursor_warp_absolute(server.cursor.base, &event->pointer->base, event->x, event->y);
    handle_motion_shared(event->time_msec);
}

static void
handle_set_cursor(struct wl_listener *listener, void *data) {
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    struct wlr_seat_client *focused_client = server.seat.base->pointer_state.focused_client;
    if(focused_client == event->seat_client) {
        wlr_cursor_set_surface(server.cursor.base, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

struct output *
cursor_get_output(void) {
    struct wlr_output *wlr_output =
            wlr_output_layout_output_at(server.output_layout, server.cursor.base->x, server.cursor.base->y);
    if(wlr_output == NULL)
        return NULL;

    return wlr_output->data;
}

struct view *
cursor_get_view(void) {
    double sx, sy;
    struct wlr_surface *surface;
    return view_at(server.cursor.base->x, server.cursor.base->y, &surface, &sx, &sy);
}

struct toplevel *
cursor_get_toplevel(void) {
    struct view *view = cursor_get_view();
    if(view == NULL)
        return NULL;

    return view_try_get_toplevel(view);
}

void
cursor_handle_focus(uint32_t time, bool handle_keyboard_focus) {
    // find the view under the pointer and send the event along
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct view *view = view_at(server.cursor.base->x, server.cursor.base->y, &surface, &sx, &sy);
    if(view == NULL) {
        cursor_set_image("default");
        wlr_seat_pointer_clear_focus(server.seat.base);
        return;
    }

    if(view->type == VIEW_TITLEBAR_CLOSE_BUTTON) {
        cursor_set_image("pointer");
        wlr_seat_pointer_clear_focus(server.seat.base);
    } else if(view->type == VIEW_TITLEBAR_BASE) {
        cursor_set_image("default");
        wlr_seat_pointer_clear_focus(server.seat.base);
    }

    // since not all views are backed by a surface (e.g. border or titlebar), we need to check for NULL
    if(surface != NULL) {
        struct wlr_pointer_constraint_v1 *constraint = wlr_pointer_constraints_v1_constraint_for_surface(
                server.constraint_manager.base, surface, server.seat.base);
        if(constraint == NULL) {
            if(server.constraint_manager.current_constraint != NULL) {
                constraint_remove_current();
            }
        } else {
            constraint_set_as_current(constraint);
        }

        wlr_seat_pointer_notify_enter(server.seat.base, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server.seat.base, time, sx, sy);
    }

    if(handle_keyboard_focus) {
        focus_view(view);
    }
}

static inline void
set_xcursor_variables(char *theme, int size) {
    char cursor_size[8];
    snprintf(cursor_size, sizeof(cursor_size), "%d", size);
    setenv("XCURSOR_SIZE", cursor_size, true);

    if(theme != NULL) {
        setenv("XCURSOR_THEME", theme, true);
    } else {
        setenv("XCURSOR_THEME", "", true);
    }
}

void
cursor_set_image(const char *name) {
    wlr_cursor_set_xcursor(server.cursor.base, server.cursor.theme_manager, name);
}

void
cursor_set_theme(char *theme, int size) {
    if(server.cursor.theme_manager != NULL) {
        wlr_xcursor_manager_destroy(server.cursor.theme_manager);
    }

    server.cursor.theme_manager = wlr_xcursor_manager_create(theme, size);
    set_xcursor_variables(theme, size);
}

static struct output *
get_primary_output(struct toplevel *toplevel) {
    struct wlr_box intersection_box;
    struct wlr_box output_box;

    uint32_t max_area = 0;
    struct output *max_area_output = NULL;

    struct output *o;
    wl_list_for_each(o, &server.outputs, link) {
        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);
        bool intersects = wlr_box_intersection(&intersection_box, &toplevel->deco_box, &output_box);
        if(intersects && box_area(&intersection_box) > max_area) {
            max_area = box_area(&intersection_box);
            max_area_output = o;
        }
    }

    return max_area_output;
}

void
cursor_stop_move_resize(void) {
    if(server.mode == SERVER_MODE_MOVING || server.mode == SERVER_MODE_RESIZING) {
        // `layout_insert_toplevel_at()` handler function may call `toplevel_set_state()` which doesnt animate state if
        // the toplevel is the same as `server.grabbed_toplevel`, so we remove it first
        struct toplevel *toplevel = server.grabbed_toplevel;
        server.grabbed_toplevel = NULL;

        if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
            struct output *primary_output = get_primary_output(toplevel);

            // we set this outputs active workspace as toplevels workspace
            toplevel->workspace = primary_output->active_workspace;
            wl_list_insert(primary_output->active_workspace->floating.next, &toplevel->link);
            // reparent it back
            wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
            // and restore the previous stacking
            toplevel_raise_to_top(toplevel);
            rules_update_for_toplevel(toplevel);
        } else {
            wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
            // here the only possible mode is moving, so we insert it into the layout
            layout_insert_toplevel_at(toplevel, server.cursor.base->x, server.cursor.base->y);
        }
    }

    // we reset the server mode to normal
    server.mode = SERVER_MODE_NORMAL;

    // clear the focus and then give it immediatelly so the client requests a new cursor image, since the server might
    // have been the one who initialized this action and who set the cursor image
    wlr_seat_pointer_clear_focus(server.seat.base);
    cursor_handle_focus(get_now_in_ms(), false);
}

void
cursor_warp_output(struct output *output) {
    if(server.config->cursor.warp > CURSOR_WARP_ON_OUTPUT_CHANGE)
        return;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_cursor_warp(server.cursor.base, NULL, output_box.x + output_box.width / 2.0,
            output_box.y + output_box.height / 2.0);
}

void
cursor_warp_toplevel(struct toplevel *toplevel, struct output *from_output) {
    if(server.config->cursor.warp == CURSOR_WARP_NONE ||
            (server.config->cursor.warp == CURSOR_WARP_ON_OUTPUT_CHANGE && toplevel->workspace->output == from_output))
        return;

    wlr_cursor_warp(server.cursor.base, NULL, toplevel->deco_box.x + toplevel->deco_box.width / 2.0,
            toplevel->deco_box.y + toplevel->deco_box.height / 2.0);

    cursor_handle_focus(get_now_in_ms(), false);
}

void
cursor_init(void) {
    server.cursor.base = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor.base, server.output_layout);

    cursor_set_theme(server.config->cursor.theme, server.config->cursor.size);

    server.cursor.motion.notify = handle_motion;
    wl_signal_add(&server.cursor.base->events.motion, &server.cursor.motion);

    server.cursor.motion_absolute.notify = handle_motion_absolute;
    wl_signal_add(&server.cursor.base->events.motion_absolute, &server.cursor.motion_absolute);

    server.cursor.button.notify = handle_button;
    wl_signal_add(&server.cursor.base->events.button, &server.cursor.button);

    server.cursor.axis.notify = handle_axis;
    wl_signal_add(&server.cursor.base->events.axis, &server.cursor.axis);

    server.cursor.frame.notify = handle_frame;
    wl_signal_add(&server.cursor.base->events.frame, &server.cursor.frame);

    server.cursor.set_cursor.notify = handle_set_cursor;
    wl_signal_add(&server.seat.base->events.request_set_cursor, &server.cursor.set_cursor);
}
