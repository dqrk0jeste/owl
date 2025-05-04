#include "session_lock.h"

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "layer_surface.h"
#include "layout.h"
#include "mwc.h"
#include "rendering.h"
#include "toplevel.h"
#include "view.h"

extern struct server server;

static void
lock_surface_handle_map(struct wl_listener *listener, void *data) {
    struct lock_surface *lock_surface = wl_container_of(listener, lock_surface, map);

    focus_lock_surface(lock_surface);
}

static void
lock_surface_handle_unmap(struct wl_listener *listener, void *data) {
    struct lock_surface *lock_surface = wl_container_of(listener, lock_surface, unmap);

    wl_list_remove(&lock_surface->link);

    // we pass focus only if the thing is still locked
    if(lock_surface->lock->locked && !wl_list_empty(&lock_surface->lock->surfaces)) {
        struct lock_surface *next = wl_container_of(lock_surface->lock->surfaces.next, next, link);
        focus_lock_surface(next);
    }
}

static void
lock_surface_handle_destroy(struct wl_listener *listener, void *data) {
    struct lock_surface *lock_surface = wl_container_of(listener, lock_surface, destroy);

    free(lock_surface);
}

void
session_lock_handle_new_surface(struct wl_listener *listener, void *data) {
    struct lock *lock = wl_container_of(listener, lock, new_surface);

    struct wlr_session_lock_surface_v1 *wlr_lock_surface = data;

    struct lock_surface *lock_surface = calloc(1, sizeof(*lock_surface));
    lock_surface->wlr_lock_surface = wlr_lock_surface;

    wl_list_insert(&lock->surfaces, &lock_surface->link);

    wlr_lock_surface->data = lock_surface;
    lock_surface->lock = lock;

    lock_surface->scene_tree = wlr_scene_subsurface_tree_create(server.session_lock_tree, wlr_lock_surface->surface);
    view_create_for_node(&lock_surface->scene_tree->node, VIEW_LOCK_SURFACE, lock_surface);

    lock_surface->map.notify = lock_surface_handle_map;
    wl_signal_add(&wlr_lock_surface->surface->events.map, &lock_surface->map);

    lock_surface->unmap.notify = lock_surface_handle_unmap;
    wl_signal_add(&wlr_lock_surface->surface->events.unmap, &lock_surface->unmap);

    lock_surface->destroy.notify = lock_surface_handle_destroy;
    wl_signal_add(&wlr_lock_surface->surface->events.destroy, &lock_surface->destroy);

    struct output *output = wlr_lock_surface->output->data;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_scene_node_set_position(&lock_surface->scene_tree->node, output_box.x, output_box.y);
    wlr_session_lock_surface_v1_configure(wlr_lock_surface, output_box.width, output_box.height);
}

void
focus_lock_surface(struct lock_surface *lock_surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
    if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat, lock_surface->wlr_lock_surface->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
    }
}

static void
restore_focus(void) {
    if(try_focus_exclusive_layer_surface())
        return;

    struct output *output = cursor_get_output();
    if(output == NULL)
        return;

    if(has_floating(server.active_workspace)) {
        focus_toplevel(first_floating(server.active_workspace), false);
    } else if(has_masters(server.active_workspace)) {
        focus_toplevel(first_master(server.active_workspace), false);
    }
}

static void
session_lock_handle_unlock(struct wl_listener *listener, void *data) {
    struct lock *lock = wl_container_of(listener, lock, unlock);

    server.lock = NULL;
    server.mode = SERVER_MODE_NORMAL;
    lock->locked = false;

    restore_focus();

    struct output *iter;
    wl_list_for_each(iter, &server.outputs, link) {
        // destroy the rectangle blocking the view
        wlr_scene_node_destroy(&iter->session_lock_rect->node);
        iter->session_lock_rect = NULL;
    }
}

static void
session_lock_handle_destroy(struct wl_listener *listener, void *data) {
    struct lock *lock = wl_container_of(listener, lock, destroy);

    if(lock->locked) {
        wlr_log(WLR_ERROR, "lock surface destroyed without being unlocked");
    }

    wl_list_remove(&lock->destroy.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->new_surface.link);

    free(lock);
}

void
session_lock_manager_handle_new(struct wl_listener *listener, void *data) {
    struct wlr_session_lock_v1 *wlr_lock = data;

    if(server.lock != NULL) {
        wlr_log(WLR_ERROR, "session already locked");
        wlr_session_lock_v1_destroy(wlr_lock);
        return;
    }

    struct lock *lock = calloc(1, sizeof(*lock));
    lock->wlr_lock = wlr_lock;
    lock->locked = true;
    wl_list_init(&lock->surfaces);

    server.lock = lock;
    // we want to set current server mode to locked, but before that we cancel the other modes
    if(server.mode == SERVER_MODE_MOVING || server.mode == SERVER_MODE_RESIZING) {
        cursor_stop_move_resize();
    }
    // todo: add canceling for other things
    server.mode = SERVER_MODE_LOCKED;

    // clear all focus on the keyboard; note: this is a bit of a hacky way of saying unfocus anything, but we dont want
    // the keyboard input going anywhere but to the lock screen surfaces when they are mapped
    wlr_seat_keyboard_notify_clear_focus(server.seat);

    // we also deactivate the focused toplevel and clear the focused layer surface so there are no complications there
    unfocus_focused_toplevel();
    server.focused_layer_surface = NULL;

    // draw black rects over the screens
    struct output *iter;
    wl_list_for_each(iter, &server.outputs, link) {
        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, iter->wlr_output, &output_box);

        iter->session_lock_rect = wlr_scene_rect_create(server.session_lock_tree, output_box.width, output_box.height,
                (float[4]){0.0, 0.0, 0.0, 1.0});
        wlr_scene_node_set_position(&iter->session_lock_rect->node, output_box.x, output_box.y);
    }

    lock->new_surface.notify = session_lock_handle_new_surface;
    wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);

    lock->unlock.notify = session_lock_handle_unlock;
    wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);

    lock->destroy.notify = session_lock_handle_destroy;
    wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

    wlr_session_lock_v1_send_locked(wlr_lock);
}

void
session_lock_manager_handle_destroy(struct wl_listener *listener, void *data) {
    wl_list_remove(&server.lock_manager_destroy.link);
    wl_list_remove(&server.new_lock.link);
}
