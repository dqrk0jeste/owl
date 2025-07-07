#include "session_lock.h"

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "layer_shell.h"
#include "layout.h"
#include "mwc.h"
#include "toplevel.h"
#include "view.h"

extern struct server server;

static void
handle_map(struct wl_listener *listener, void *data) {
    struct lock_surface *surface = wl_container_of(listener, surface, map);

    focus_lock_surface(surface);
}

static void
handle_unmap(struct wl_listener *listener, void *data) {
    struct lock_surface *surface = wl_container_of(listener, surface, unmap);

    wl_list_remove(&surface->link);

    // we pass focus only if the thing is still locked
    if(surface->lock->locked && !wl_list_empty(&surface->lock->surfaces)) {
        struct lock_surface *next = wl_container_of(surface->lock->surfaces.next, next, link);
        focus_lock_surface(next);
    }
}

static void
surface_handle_destroy(struct wl_listener *listener, void *data) {
    struct lock_surface *surface = wl_container_of(listener, surface, destroy);

    free(surface);
}

static void
handle_new_surface(struct wl_listener *listener, void *data) {
    struct lock *lock = wl_container_of(listener, lock, new_surface);

    struct wlr_session_lock_surface_v1 *wlr_lock_surface = data;

    struct lock_surface *lock_surface = calloc(1, sizeof(*lock_surface));
    lock_surface->wlr_lock_surface = wlr_lock_surface;

    wl_list_insert(&lock->surfaces, &lock_surface->link);

    wlr_lock_surface->data = lock_surface;
    lock_surface->lock = lock;

    lock_surface->scene_tree = wlr_scene_subsurface_tree_create(server.session_lock_tree, wlr_lock_surface->surface);
    view_create_for_node(&lock_surface->scene_tree->node, VIEW_LOCK_SURFACE, lock_surface);

    lock_surface->map.notify = handle_map;
    wl_signal_add(&wlr_lock_surface->surface->events.map, &lock_surface->map);

    lock_surface->unmap.notify = handle_unmap;
    wl_signal_add(&wlr_lock_surface->surface->events.unmap, &lock_surface->unmap);

    lock_surface->destroy.notify = surface_handle_destroy;
    wl_signal_add(&wlr_lock_surface->surface->events.destroy, &lock_surface->destroy);

    struct output *output = wlr_lock_surface->output->data;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_scene_node_set_position(&lock_surface->scene_tree->node, output_box.x, output_box.y);
    wlr_session_lock_surface_v1_configure(wlr_lock_surface, output_box.width, output_box.height);
}

static void
restore_focus(void) {
    if(try_focus_exclusive_layer_surface())
        return;

    struct output *output = cursor_get_output();
    if(output == NULL)
        return;

    if(server.active_workspace->fullscreen != NULL) {
        focus_toplevel(server.active_workspace->fullscreen, false);
    } else if(has_floating(server.active_workspace)) {
        focus_toplevel(first_floating(server.active_workspace), false);
    } else if(has_masters(server.active_workspace)) {
        focus_toplevel(first_master(server.active_workspace), false);
    }
}

static void
handle_unlock(struct wl_listener *listener, void *data) {
    struct lock *lock = wl_container_of(listener, lock, unlock);

    server.lock_manager.current_lock = NULL;
    server.mode = SERVER_MODE_NORMAL;
    lock->locked = false;

    restore_focus();

    struct output *iter;
    wl_list_for_each(iter, &server.outputs, link) {
        wlr_scene_node_set_enabled(&iter->session_lock_rect->node, false);
    }
}

static void
lock_handle_destroy(struct wl_listener *listener, void *data) {
    struct lock *lock = wl_container_of(listener, lock, destroy);

    if(lock->locked) {
        wlr_log(WLR_ERROR, "lock surface destroyed without being unlocked");
    }

    wl_list_remove(&lock->destroy.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->new_surface.link);

    free(lock);
}

static void
handle_new_lock(struct wl_listener *listener, void *data) {
    struct wlr_session_lock_v1 *wlr_lock = data;

    if(server.lock_manager.current_lock != NULL) {
        wlr_log(WLR_INFO, "session already locked");
        wlr_session_lock_v1_destroy(wlr_lock);
        return;
    }

    struct lock *lock = calloc(1, sizeof(*lock));
    lock->wlr_lock = wlr_lock;
    lock->locked = true;
    wl_list_init(&lock->surfaces);

    // we want to set current server mode to locked, but before that we cancel the other modes
    // todo: see how to cancel drag if any
    if(server.mode == SERVER_MODE_MOVING || server.mode == SERVER_MODE_RESIZING ||
            server.mode == SERVER_MODE_RESIZING_MASTER_RATIO) {
        cursor_stop_move_resize();
    }

    server.lock_manager.current_lock = lock;
    server.mode = SERVER_MODE_LOCKED;

    // clear all focus on the keyboard; note: this is a bit of a hacky way of saying unfocus anything, but we dont want
    // the keyboard input going anywhere but to the lock screen surfaces when they are mapped
    wlr_seat_keyboard_notify_clear_focus(server.seat.base);

    // we also deactivate the focused toplevel and clear the focused layer surface so there are no complications there
    unfocus_focused_toplevel();
    server.focused_layer_surface = NULL;

    // show black rects over the screens
    struct output *iter;
    wl_list_for_each(iter, &server.outputs, link) {
        wlr_scene_node_set_enabled(&iter->session_lock_rect->node, true);
    }

    lock->new_surface.notify = handle_new_surface;
    wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);

    lock->unlock.notify = handle_unlock;
    wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);

    lock->destroy.notify = lock_handle_destroy;
    wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

    wlr_session_lock_v1_send_locked(wlr_lock);
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    wl_list_remove(&server.lock_manager.destroy.link);
    wl_list_remove(&server.lock_manager.new_lock.link);
}

void
lock_manager_init(void) {
    server.lock_manager.base = wlr_session_lock_manager_v1_create(server.display);

    server.lock_manager.new_lock.notify = handle_new_lock;
    wl_signal_add(&server.lock_manager.base->events.new_lock, &server.lock_manager.new_lock);

    server.lock_manager.destroy.notify = handle_destroy;
    wl_signal_add(&server.lock_manager.base->events.destroy, &server.lock_manager.destroy);
}

void
focus_lock_surface(struct lock_surface *lock_surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat.base);
    if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat.base, lock_surface->wlr_lock_surface->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
    }
}
